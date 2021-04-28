#include <vector>
#include <algorithm>
#include <thread>
#include <assert.h>
#include <memory>
#include <atomic>
#include <functional>
#include <chrono>
#include <optional>
#include <iostream>
#include <variant>
#include <array>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <iterator>
#include <concepts>

namespace async{

    class exception : public std::exception {
        public:
        exception(const char* msg) : msg(msg) {}
        virtual const char* what() const noexcept 
            { return msg; }
        protected:
        const char* msg;
    };

    class DanglingPromise : public exception {
        public:
        DanglingPromise() : exception(
"All promises must be awaited, and/or stored correctly until they are satisfied. Do not allow Promise objects to become temporaries unawaited."
            ) {}
    };

    /**
     * The Promise class designates a future location for the return value of a coroutine.
     * A Promise will designate one object of type T.
     * If a Promise is expecting to be fullfilled, do NOT allow it to be a temporary or it will become
     * a dangling promise and an error will be thrown.
     */
    template<typename T>
    class _Promise{

        public:
        /**
         * maybeT represents an object that might contain T, or not.
         */
        typedef std::optional<T> maybeT;
        /**
         * ThenType represents a function that can be called once the Promise is satisfied.
         */
        typedef std::function< maybeT (T&)> Thentype;
        /**
         * The type a Promise is meant to hold.
         */
        typedef T type; 
        _Promise() = default; 
        _Promise(const _Promise<T>& other) : element(other.element), thenfn(other.thenfn) { set = (bool)other.set; };
        ~_Promise() = default;
        /**
         * await() will block the thread until this promise is fulfilled.
         * @returns the value assigned to this promise;
         */
        T& await()
            { while(!set){} return **element; }
        /**
         * 
         */
        maybeT block_then( Thentype fn ) 
            {  return fn(await()); }
        
        maybeT wait_for(std::chrono::milliseconds ms)
        { 
            auto now = std::chrono::system_clock::now();
            while(!set and std::chrono::system_clock::now()-now<ms){}
            if(set) return **element ;
            else return std::nullopt;
        }
        
        void assign(maybeT value)
        { 
            if(element.get() == nullptr){
                element = std::make_shared<maybeT>(value);
            }else{
                *element =  (value); 
            }
            set = true; 
        }

        bool done(){
            return set;
        }

        


        private:
        std::shared_ptr<maybeT> element ;
        std::atomic<bool> set = false;
        std::optional<Thentype> thenfn = std::nullopt;
    };

    template<typename T>
    using Promise = std::shared_ptr<_Promise<T > >; 

    template<typename T> 
    class Queue{
        public:
        typedef T element_t; 
        Queue() = default;
        Queue(const Queue& q) : base(q.base) {};
        Queue(size_t size) : base(size) {}

        void enqueue(const T& x) 
        { 
            std::lock_guard<std::mutex> lock(mut);
            base.emplace(x); 
        }

        bool empty()
        {
            std::lock_guard<std::mutex> lock(mut);
            bool out = base.empty();
            return out;
        }

        auto dequeue(){
            std::lock_guard<std::mutex> lock(mut);
            auto out = base.front();
            base.pop();
            return out;
        }

        std::optional<T> safeget(){
            std::lock_guard<std::mutex> lock(mut);
            if(!base.empty()){
                auto out =  base.front();
                base.pop();
                return out;
            }

            return std::nullopt;
        }

        private:
        std::queue<T> base;
        mutable std::mutex mut;
    };

    
    class EventLoop{

        public:
        std::atomic<size_t> activeThreads;
        std::atomic<bool> isActive = true;
        EventLoop(size_t workers = 4)
        {   
            worker_count = workers;
            for(size_t i = 0; i < worker_count; i++){
                threadpool.push_back( std::thread( worker_mainloop, task_handle, this ) );
            }
        }

        EventLoop(const EventLoop& other) = delete;
        ~EventLoop(){
            isActive = false;
            for(auto& t: threadpool){
                t.join();
            }
        
        }
        template<typename T>
        [[nodiscard]] Promise<T> execute( std::function<T ()> coroutine) 
            {
                _Promise<T> out;
                Promise<T> ptr = std::make_shared<_Promise<T> >(out);
                std::weak_ptr<_Promise<T> > weak = ptr;
                if (activeThreads == threadpool.size()) // if all threads are occupied
                {
                    threadpool.push_back(std::thread( worker_mainloop, task_handle, this ));
                    worker_count++;
                }
                task_handle->enqueue( 
                    [coroutine, weak](){ 

                        auto v = coroutine();
                        // Check to ensure that the promise is still waiting on the other end
                        if (weak.expired()){
                            // Throw if not
                            throw DanglingPromise();
                        }
                        // Assign the value now that the promise is confirmed to exist
                        weak.lock()->assign(v);
                    }
                );
                return ptr;
            }
        [[nodiscard]] Promise<bool> execute( std::function<void(void)> coroutine )
            { return execute<bool>( [=](){ coroutine(); return true;} ); }

        
        


        private:
        typedef std::shared_ptr< Queue< std::function<void (void)> > > task_handle_t;
        typedef task_handle_t::weak_type weak_task_t;
        typedef Queue<task_handle_t::element_type::element_t> queue_t;
        size_t worker_count = 4;
        std::vector<std::thread> threadpool;

        task_handle_t task_handle = std::make_shared< queue_t >( queue_t() );


        static void worker_mainloop(weak_task_t activeSignal, EventLoop* rawParent){
            while(!activeSignal.expired()){
                if (rawParent->isActive == false){
                    exit(0); // exit thread;
                }
                std::optional<std::function<void ()>> maybeTask;
                {
                    auto tasks = activeSignal.lock();
                    maybeTask = tasks->safeget();
                }
                if (maybeTask) {
                    rawParent->activeThreads++;
                    (*maybeTask)();
                    rawParent->activeThreads--;
                }
                std::this_thread::sleep_for( std::chrono::milliseconds(1) );
            }
        }

       

    };


    namespace algorithm{
        
        template<class InputIterator, typename Function> 
        //TODO Fix std::invokable for lambda/funs
            requires std::input_iterator<InputIterator>
        [[nodiscard]] Promise<bool> to_each(EventLoop& e, InputIterator begin, InputIterator end, Function fn){
            
            std::vector<Promise<bool> > promises;
            promises.reserve(end-begin);

            std::for_each(begin, end, [&e,fn, begin, end, &promises](auto& i){
                promises.push_back(
                    (e.execute(
                        [&](){
                            fn(i);
                        }
                    ))
                );
            
            });

            return e.execute( [=](){ 
                for (auto& p : promises ) {
                    p->await();
                    }
                } 
            );

        }


    }





    //EventLoop<4> mainloop;

};
