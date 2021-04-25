#include "../src/async.hpp"
#include <future>
int main(){
    

    auto dispose = async::mainloop.execute<int>( [](){ std::this_thread::sleep_for(std::chrono::seconds(1)); return 5; } );
    async::mainloop.execute( [](){ std::this_thread::sleep_for(std::chrono::seconds(100)); } );
    puts("Here");

    std::cout << dispose.await() << std::endl;

    return 0;
}