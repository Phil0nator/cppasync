#include "async.hpp"



int main() {

    
    async::EventLoop loop{std::thread::hardware_concurrency()};
    async::default_loop(&loop);

    async::run([](){
        printf("Hello World! (1)\n");
        return 5;
    });

    async::run([](){
        printf("Hello World! (2)\n");
    });


    auto test = async::run_later(1000, [](){
        printf("Hello World! (3)\n");
        return 5;
    }).then([](int a){
        printf("its over now :)\n");
        return 3.5+a;
    });



    double result = test.get();
    printf("Final result: %g\n", result);


    return 0;
}