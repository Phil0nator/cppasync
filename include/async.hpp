
#include <queue>
#include <functional>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <iostream>
#include <list>
#include <vector>
#include <stdexcept>
#include <set>
#include <optional>

namespace async {

    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    
    class EventLoop;

    template<typename T>
    class Future;

    template<typename T>
    class Promise : public std::promise<T>
    {
        public:

        explicit Promise(EventLoop* lp): std::promise<T>(), lp(lp) {}
        Promise(const Promise<T>& other) = delete;
        
        
        // Setting the result
        inline void invoke_chain();

        inline void set_value()
        {
            std::unique_lock lock(mtx);
            std::promise<T>::set_value();
            invoke_chain();
        }

        inline void set_value(const T& __r)
        { 
            std::unique_lock lock(mtx);
            std::promise<T>::set_value(__r); 
            invoke_chain();
        }

        inline void set_value(T&& __r)
        { 
            std::unique_lock lock(mtx);
            std::promise<T>::set_value(std::move(__r));   
            invoke_chain();
        }

        inline void set_exception(std::exception_ptr __p)
        { 
            std::unique_lock lock(mtx);
            std::promise<T>::set_exception(__p);   
        }


        private:
        friend class Future<T>;
        std::mutex mtx;
        std::function<void()> chain;
        EventLoop* lp;
    };

    template<>
    class Promise<void>: public std::promise<void>
    {
        public:

        explicit Promise(EventLoop* lp): std::promise<void>(), lp(lp) {}
        Promise(const Promise<void>& other) = delete;


        void invoke_chain();

        inline void
        set_value()
        {
            std::unique_lock lock(mtx);
            std::promise<void>::set_value();
            invoke_chain();
            
        }
        inline void
        set_exception(std::exception_ptr __p)
        { 
            std::unique_lock lock(mtx);
            std::promise<void>::set_exception(__p);   
        }

        private:
        friend class Future<void>;
        std::mutex mtx;
        std::function<void()> chain;
        EventLoop* lp;

    };

    template<typename T>
    class Future : public std::future<T>
    {
        public:

        Future(const std::shared_ptr<Promise<T>>& p, EventLoop* loop)
            : std::future<T>(p->get_future()), promise(p), loop(loop)
            {}

        template<typename F>
        auto then(F&& f) && -> Future<typename std::invoke_result<F, T>::type>;

        inline T get() {
            this->promise.reset();
            return std::future<T>::get();
        }

        private:
        EventLoop* loop;
        std::weak_ptr<Promise<T>> promise;
    };

    class EventLoop {

        public:

        /**
         * @brief Construct a new Event Loop object with num_threads
         * 
         * @param num_threads number of threads
         */
        explicit EventLoop(size_t num_threads = 1) : running(false) {
            if (num_threads == 0) {
                throw std::invalid_argument("Number of threads must be greater than 0");
            }
            start_workers(num_threads);
        }

        EventLoop(const EventLoop& other) = delete;
        EventLoop operator=(const EventLoop& other) = delete;

        ~EventLoop() {
            stop();
            for (auto& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
        }

        /**
         * @brief Stop the event loop
         * 
         */
        void stop() {
            {
                std::lock_guard<std::mutex> lock(mutex);
                running = false;
            }
            condition.notify_all();
        }

        /**
         * @brief Run a task asynchronously, returning a value or exception with a std::future
         * 
         * @tparam F Invokable type to run
         * @tparam Args types of arguments provided to the invokable
         * @param f Invokable object to run
         * @param args Arguments provided to f
         * @return std::future return value or exception 
         */
        template<typename F, typename... Args>
        auto run(F&& f, Args&&... args) -> Future<typename std::invoke_result<F, Args...>::type>
        {
            using ReturnType = typename std::invoke_result<F, Args...>::type;
            auto promise = std::make_shared<Promise<ReturnType>>(this);
            // std::future<ReturnType> future = promise->get_future();
            Future<ReturnType> future = Future<ReturnType>(promise, this);
            auto task = [promise, f = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable {
                try {
                    if constexpr (std::is_void<ReturnType>::value) {
                        f(args...);
                        promise->set_value();
                    } else {
                        promise->set_value(f(args...));
                    }
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            };

            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!running) {
                    throw std::runtime_error("Cannot enqueue task: event loop is stopped");
                }
                tasks.push(std::move(task));
            }
            condition.notify_one();
            return future;
        }

        /**
         * @brief Schedule a task to run after some delay
         * 
         * @tparam F Invokable type to run
         * @tparam Args Argument types for F
         * @param delay_ms delay in milliseconds
         * @param f Invokable object to run
         * @param args Arguments for f
         * @return std::future return value or exception
         */
        template<typename F, typename... Args>
        auto run_later(int delay_ms, F&& f, Args&&... args) -> Future<typename std::invoke_result<F, Args...>::type> {
            using ReturnType = typename std::invoke_result<F, Args...>::type;
            auto promise = std::make_shared<Promise<ReturnType>>(this);
            Future<ReturnType> future(promise, this);
            
            auto task = [promise, f = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable {
                try {
                    if constexpr (std::is_void<ReturnType>::value) {
                        f(args...);
                        promise->set_value();
                    } else {
                        promise->set_value(f(args...));
                    }
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            };

            auto deadline = Clock::now() + std::chrono::milliseconds(delay_ms);
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!running) {
                    throw std::runtime_error("Cannot schedule task: event loop is stopped");
                }
                timers.emplace(Timer(deadline, std::move(task)));
            }
            condition.notify_one();
            return future;
        }

        private:

        class Timer {
            public:
            TimePoint deadline;
            std::function<void()> task;
            Timer(TimePoint d, std::function<void()> t) : deadline(d), task(std::move(t)) {}
            
            Timer(const Timer& other) = delete;
            Timer(Timer&& other) = default;

            auto operator <=>(const Timer& other) const noexcept 
                { return deadline <=> other.deadline; }
        };

       
        void start_workers(size_t num_threads) {
            running = true;
            for (size_t i = 0; i < num_threads; ++i) {
                workers.emplace_back([this]() { worker_loop(); });
            }
        }


        void worker_loop() {
            while (running) {
                std::function<void()> task;
                bool has_task = false;
                auto now = Clock::now();

                // Process timers
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!timers.empty() && timers.begin()->deadline <= now) {
                        task = std::move((timers.extract(timers.begin()).value().task));
                        has_task = true;
                    }
                }

                // Process immediate tasks
                if (!has_task) {
                    std::unique_lock<std::mutex> lock(mutex);
                    if (!tasks.empty()) {
                        task = std::move(tasks.front());
                        tasks.pop();
                        has_task = true;
                    } else if (!timers.empty()) {
                        condition.wait_until(lock, timers.begin()->deadline, [this] {
                            return !tasks.empty() || !running;
                        });
                        continue;
                    } else {
                        condition.wait(lock, [this] { return !tasks.empty() || !timers.empty() || !running; });
                        continue;
                    }
                }

                // Execute the task
                if (has_task) {
                    task();
                }

            }
        }

        std::queue<std::function<void()>> tasks;
        std::set<Timer> timers;
        std::mutex mutex;
        std::condition_variable condition;
        bool running;
        std::vector<std::thread> workers;
    };

    template<typename T>
    template<typename F>
    auto Future<T>::then(F&& f) && -> Future<typename std::invoke_result<F, T>::type>
    {
        using ReturnType = typename std::invoke_result<F, T>::type;
        std::shared_ptr<Promise<T>> parent;
        if ((parent = promise.lock()) == nullptr ){
            if constexpr (std::is_void<T>::value) {
                loop->run(f);
            } else {
                return loop->run(f, Future<T>::get());
            }
        } 

        std::unique_lock lock(parent->mtx);

        if (std::future<T>::wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            if constexpr (std::is_void<T>::value) {
                loop->run(f);
            } else {
                return loop->run(f, Future<T>::get());
            }
        }

        auto np = std::make_shared<Promise<ReturnType>>(loop);
        Future<ReturnType> future(np, loop);
        
        auto chain = [self=this->share(), np, f = std::forward<F>(f)]() mutable {
            try {
                if constexpr (std::is_void<ReturnType>::value) {
                    f();
                    np->set_value();
                } else {
                    np->set_value(f(self.get()));
                }
            } catch (...) {
                np->set_exception(std::current_exception());
            }
        };

        parent->chain = std::move(chain);

        return future;
    }

    template <typename T>
    inline void async::Promise<T>::invoke_chain()
    {
        if (chain) {
            lp->run(chain);
        }
    }

    inline void async::Promise<void>::invoke_chain()
    {
        if (chain) {
            lp->run(chain);
        }
    }



}