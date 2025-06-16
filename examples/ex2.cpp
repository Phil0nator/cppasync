#include "async.hpp"


async::Future<void> slow_task(async::EventLoop& loop) {
    return loop.run([](){
        for(size_t i = 0; i < 1000; i++) {
            if (i % 100 == 0) printf("%d\n", i);
        }
    });
}


int main() {

    async::EventLoop loop{1};

    loop.run([&loop](){
        auto task_a = slow_task(loop);
        auto task_b = slow_task(loop);

        task_a.yield_until_ready();
        task_b.yield_until_ready();
    }).get();

    return 0;
}