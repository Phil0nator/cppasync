#include <queue>
#include <functional>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <vector>
#include <stdexcept>
#include <set>
#include <compare>

namespace async {

    /**
     * @brief Abbreviation for std::chrono::steady_clock
     */
    using Clock = std::chrono::steady_clock;
    /**
     * @brief Abbreviation for std::chrono::steady_clock::time_point
     */
    using TimePoint = Clock::time_point;
    
    class EventLoop;

    template<typename T>
    class Future;

    /**
     * @brief Internal promise type that allows task chaining
     * 
     * @tparam T return type
     */
    template<typename T>
    class Promise : public std::promise<T>
    {
        public:

        /**
         * @brief Construct a new Promise object
         * 
         * @param lp parent event loop
         */
        explicit Promise(EventLoop* lp);
        Promise(const Promise<T>& other) = delete;
        
        // Setting the result

        /**
         * @brief Invoke the chained task, if present
         */
        inline void invoke_chain();

        /**
         * @brief Set the value object (void)
         *  marking the task complete
         */
        inline void set_value();

        /**
         * @brief Set the value object (copy construct)
         *  marking the task complete
         */
        inline void set_value(const T& __r);

        /**
         * @brief Set the value object (move)
         *  marking the task complete
         */
        inline void set_value(T&& __r);

        /**
         * @brief Set the exception
         *  marking the task complete
         */
        inline void set_exception(std::exception_ptr __p);

        private:
        friend class Future<T>;
        std::mutex mtx;
        std::function<void()> chain;
        EventLoop* lp;
    };

    /**
     * @brief Promise specialization for void return types
     */
    template<>
    class Promise<void> : public std::promise<void>
    {
        public:
        /**
         * @brief Construct a new Promise object
         * 
         * @param lp parent event loop
         */
        explicit Promise(EventLoop* lp);
        Promise(const Promise<void>& other) = delete;

        /**
         * @brief Invoke the chained task, if present
         */
        inline void invoke_chain();

        /**
         * @brief Set the value object (void)
         *  marking the task complete
         */
        inline void set_value();

        /**
         * @brief Set the exception
         *  marking the task complete
         */
        inline void set_exception(std::exception_ptr __p);

        private:
        friend class Future<void>;
        std::mutex mtx;
        std::function<void()> chain;
        EventLoop* lp;
    };


    /**
     * @brief Future type allowing task chaining
     * 
     * @tparam T return type
     */
    template<typename T>
    class Future : public std::future<T>
    {
        public:

        /**
         * @brief Construct a new Future object
         * 
         * @param p source promise
         * @param loop parent loop
         */
        Future(const std::shared_ptr<Promise<T>>& p, EventLoop* loop);

        /**
         * @brief Chain a task to enqueue at the completion of this future
         * 
         * @tparam F Invokable type
         * @param f task function
         * @return Future<typename std::invoke_result<F, T>::type> future
         *  for the completion of `f`
         */
        template<typename F>
        auto then(F&& f) && -> Future<typename std::invoke_result<F, T>::type>;

        /**
         * @brief Block and get the value set by the promise, or
         *  throw any exception.
         * 
         * @return T return type
         */
        T get();

        /**
         * @brief Yield to the event loop until 
         * 
         * @return T the value given to this future
         */
        T yield_until_ready();

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
        explicit EventLoop(size_t num_threads = 1);


        EventLoop(const EventLoop& other) = delete;
        EventLoop operator=(const EventLoop& other) = delete;
        
        ~EventLoop();

        /**
         * @brief Stop the event loop
         */
        void stop();

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
        auto run(F&& f, Args&&... args) -> Future<typename std::invoke_result<F, Args...>::type>;

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
        auto run_later(int delay_ms, F&& f, Args&&... args) -> Future<typename std::invoke_result<F, Args...>::type>;

        private:

        /**
         * @brief Internal timer for scheduled tasks
         */
        class Timer {
            public:
            TimePoint deadline;
            std::function<void()> task;
            Timer(TimePoint d, std::function<void()> t);
            Timer(const Timer& other) = delete;
            Timer(Timer&& other) = default;
            inline std::strong_ordering operator<=>(const Timer& other) const noexcept;
        };

        inline void start_workers(size_t num_threads);
        inline void worker_loop();
        inline bool process_pending();

        std::queue<std::function<void()>> tasks;
        std::set<Timer> timers;
        std::mutex mutex;
        std::condition_variable condition;
        bool running;
        std::vector<std::thread> workers;


        template<typename T>
        friend class Future;

    };

    // Promise<T> definitions
    template<typename T>
    Promise<T>::Promise(EventLoop* lp) : std::promise<T>(), lp(lp) {}

    template<typename T>
    void Promise<T>::invoke_chain() {
        if (chain) {
            lp->run(chain);
        }
    }

    template<typename T>
    void Promise<T>::set_value() {
        std::unique_lock lock(mtx);
        std::promise<T>::set_value();
        invoke_chain();
    }

    template<typename T>
    void Promise<T>::set_value(const T& __r) { 
        std::unique_lock lock(mtx);
        std::promise<T>::set_value(__r); 
        invoke_chain();
    }

    template<typename T>
    void Promise<T>::set_value(T&& __r) { 
        std::unique_lock lock(mtx);
        std::promise<T>::set_value(std::move(__r));   
        invoke_chain();
    }

    template<typename T>
    void Promise<T>::set_exception(std::exception_ptr __p) { 
        std::unique_lock lock(mtx);
        std::promise<T>::set_exception(__p);   
    }

    // Promise<void> definitions
    Promise<void>::Promise(EventLoop* lp) : std::promise<void>(), lp(lp) {}

    void Promise<void>::invoke_chain() {
        if (chain) {
            lp->run(chain);
        }
    }

    void Promise<void>::set_value() {
        std::unique_lock lock(mtx);
        std::promise<void>::set_value();
        invoke_chain();
    }

    void Promise<void>::set_exception(std::exception_ptr __p) { 
        std::unique_lock lock(mtx);
        std::promise<void>::set_exception(__p);   
    }

    // Future<T> definitions
    template<typename T>
    Future<T>::Future(const std::shared_ptr<Promise<T>>& p, EventLoop* loop)
        : std::future<T>(p->get_future()), promise(p), loop(loop) {}


    // Generic then implementation
    template<typename T>
    template<typename F>
    auto Future<T>::then(F&& f) && -> Future<typename std::invoke_result<F, T>::type> {
        using ReturnType = typename std::invoke_result<F, T>::type;
        std::shared_ptr<Promise<T>> parent;
        if ((parent = promise.lock()) == nullptr) {
            return loop->run(f, this->get());
        } 

        std::unique_lock lock(parent->mtx);

        if (this->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            return loop->run(f, this->get());
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

    // Specialized void future then implementation
    template<>
    template<typename F>
    auto Future<void>::then(F&& f) && -> Future<typename std::invoke_result<F, void>::type> {
        using ReturnType = typename std::invoke_result<F, void>::type;
        std::shared_ptr<Promise<void>> parent;
        if ((parent = promise.lock()) == nullptr) {
            this->get();
            return loop->run(f);
        } 

        std::unique_lock lock(parent->mtx);

        if (this->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            this->get();
            return loop->run(f);
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

    template<typename T>
    T Future<T>::get() {
        this->promise.reset();
        return std::future<T>::get();
    }

    template <typename T>
    inline T Future<T>::yield_until_ready()
    {
        while (std::future<T>::wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
            if (!loop->process_pending()) {
                std::this_thread::yield();
            }
        }
        return get();
    }

    template<>
    inline void Future<void>::yield_until_ready() {
        while (std::future<void>::wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
            if (!loop->process_pending()) {
                std::this_thread::yield();
            }
        }
        get();
    }

    // EventLoop definitions
    EventLoop::EventLoop(size_t num_threads) : running(false) {
        if (num_threads == 0) {
            throw std::invalid_argument("Number of threads must be greater than 0");
        }
        start_workers(num_threads);
    }

    EventLoop::~EventLoop() {
        stop();
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void EventLoop::stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            running = false;
        }
        condition.notify_all();
    }

    template<typename F, typename... Args>
    auto EventLoop::run(F&& f, Args&&... args) -> Future<typename std::invoke_result<F, Args...>::type> {
        using ReturnType = typename std::invoke_result<F, Args...>::type;
        auto promise = std::make_shared<Promise<ReturnType>>(this);
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

    template<typename F, typename... Args>
    auto EventLoop::run_later(int delay_ms, F&& f, Args&&... args) -> Future<typename std::invoke_result<F, Args...>::type> {
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

    EventLoop::Timer::Timer(TimePoint d, std::function<void()> t) : deadline(d), task(std::move(t)) {}

    inline std::strong_ordering EventLoop::Timer::operator<=>(const Timer& other) const noexcept {
        return deadline <=> other.deadline;
    }

    void EventLoop::start_workers(size_t num_threads) {
        running = true;
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this]() { worker_loop(); });
        }
    }

    void EventLoop::worker_loop() {
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

    inline bool EventLoop::process_pending()
    {
        std::function<void()> task;
        bool has_task = false;
        auto now = Clock::now();

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!timers.empty() && timers.begin()->deadline <= now) {
                task = std::move(timers.extract(timers.begin()).value().task);
                has_task = true;
            } else if (!tasks.empty()) {
                task = std::move(tasks.front());
                tasks.pop();
                has_task = true;
            }
        }

        if (has_task) {
            task();
            return true;
        }
        return false;
    }
}