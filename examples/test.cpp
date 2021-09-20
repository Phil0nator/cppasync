#include "../src/async.hpp"


int main(){
    using namespace async;

    EventLoop loop;

    algorithm::then(

    loop,

    algorithm::to_each_range( loop, 0, 10, .0001, [](auto a) { 
        printf("%g %d\n", a, std::this_thread::get_id() ); 
        return;
    } ),

    [&]{

        printf("\nFinal thread count: %ld\n", loop.getThreadCount());

    }

    )->await();

    return 0;
}