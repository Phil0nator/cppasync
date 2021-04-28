#include "../src/async.hpp"
#include <future>

int testfn (){
    puts("testfn");
    std::this_thread::sleep_for(std::chrono::seconds(1)); 
    return 5; 
}


int main(){
    

    async::EventLoop loop(20);

    // auto dispose = loop.execute<int>( test );
    // auto dispose2 = loop.execute( [](){ 
    //     std::this_thread::sleep_for(std::chrono::seconds(10)); 
    //     } );

    // std::cout << dispose->await() << std::endl;
    // dispose2->await();
    std::array test{1,2,3,4,5,6,7,8,10};
    async::algorithm::to_each( loop, test.begin(), test.end(), [](auto& i){ 
        testfn();
        printf("%i\n",i); 
        testfn();

        } );
    async::algorithm::to_each( loop, test.begin(), test.end(), [](auto& i){ 
        
        printf("lp2:%i\n", i); 
        } );
//    disposable->await();
//    disp2->await();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}