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

#if __cplusplus < 200000L
#error "async.hpp requires c++20 or later"
#endif

namespace async{



    template<typename T>
    class Promise{

        public:
        typedef std::optional<T> maybeT;
        typedef std::function< maybeT (T&)> Thentype;
        typedef T type; 
        Promise() = default;
        Promise(const Promise<T>& other) : element(other.element), thenfn(other.thenfn) { set = (bool)other.set; };
        ~Promise() = default;
        T& await()
            { while(!set){} return **element; }
        maybeT block_then( Thentype fn ) 
            {  return fn(await()); }
        
        maybeT wait_for(std::chrono::milliseconds ms)
        { 
            auto now = std::chrono::system_clock::now();
            while(!set and std::chrono::system_clock::now()-now<ms){}
            if(set) return **element ;
            else return std::nullopt;
        }
        Promise<T> then(const Thentype& fn) 
        {
            Promise<T> out;
            thenfn = [&](T& v){ out.assign(fn(v)); return std::nullopt;};
            return out;
        }
        void assign(const maybeT& value)
        { 
            element =  std::make_shared<maybeT>(value); 
            set = true; 
            if(thenfn and *element) thenfn.operator*()(**element);
        }


        private:
        std::shared_ptr<maybeT> element;
        std::atomic<bool> set = false;
        std::optional<Thentype> thenfn = std::nullopt;
    };


    template<typename T> 
    class Queue{
        public:
        typedef T element_t; 
        Queue() = default;
        Queue(const Queue& q) : base(q.base) {};
        Queue(size_t size) : base(size) {}

        void enqueue(const T& x) 
        { 
            std::lock_guard lock(mut);
            base.emplace(x); 
        }

        bool empty()
        {
            std::lock_guard lock(mut);
            bool out = base.empty();
            return out;
        }

        auto dequeue(){
            std::lock_guard lock(mut);
            auto out = base.front();
            base.pop();
            return out;
        }

        std::optional<T> safeget(){
            std::lock_guard lock(mut);
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

    template<size_t worker_count = 4>
    class EventLoop{

        public:

        EventLoop()
        {
            for(size_t i = 0; i < worker_count; i++){
                std::thread t( worker_mainloop, task_handle );
                t.detach();
            }
        }

        EventLoop(const EventLoop& other) = delete;
        ~EventLoop() = default;

        template<typename T>
        [[nodiscard]] Promise<T> execute( std::function<T ()> coroutine) 
            {
                Promise<T> out;
                task_handle->enqueue( 
                    [&](){ out.assign(coroutine()); }
                );
                return out;
            }
        void execute( std::function<void(void)> coroutine )
            { task_handle->enqueue(coroutine); }

        private:
        typedef std::shared_ptr< Queue< std::function<void (void)> > > task_handle_t;
        
        typedef task_handle_t::weak_type weak_task_t;
        typedef Queue<task_handle_t::element_type::element_t> queue_t;
        
        task_handle_t task_handle = std::make_shared< queue_t >( queue_t() );
        static void worker_mainloop(weak_task_t activeSignal){
            while(!activeSignal.expired()){
                std::optional<std::function<void ()>> maybeTask;
                {
                    auto tasks = activeSignal.lock();
                    maybeTask = tasks->safeget();
                }
                if (maybeTask) {
                    (*maybeTask)();
                }
                std::this_thread::sleep_for( std::chrono::milliseconds(1) );
            }
        }

    };


    EventLoop<4> mainloop;

};
