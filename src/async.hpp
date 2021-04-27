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

    template<typename T>
    class _Promise{

        public:
        typedef std::optional<T> maybeT;
        typedef std::function< maybeT (T&)> Thentype;
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
    using Promise = std::unique_ptr<_Promise<T > >; 

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
                Promise<T> ptr = std::make_unique<_Promise<T> >(out);
                if (activeThreads == threadpool.size()) // if all threads are occupied
                {
                    threadpool.push_back(std::thread( worker_mainloop, task_handle, this ));
                    worker_count++;
                }
                task_handle->enqueue( 
                    [coroutine, &ptr](){ 
                        ptr->assign(coroutine());
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
            requires std::input_iterator<InputIterator> && std::invocable<Function>
        [[nodiscard]] Promise<size_t> to_each(EventLoop& e, InputIterator begin, InputIterator end, Function fn){
            
            std::for_each(begin, end-1, [&e,fn, begin, end](auto& i){
                e.execute(
                    [&](){
                        std::invoke(fn, i);
                    }
                );
            
            
            });

            return e.execute( [=](){ std::invoke(fn, *end); } );

        }


    }





    //EventLoop<4> mainloop;

};
