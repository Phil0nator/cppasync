#include "../src/async.hpp"



int main(){
    
    // Create an event loop to async tasks
    async::EventLoop loop;

    // execute a task, and get the promise
    async::Promise<int> result = loop.execute<int>( [](){
        std::cout << "This is running async." << std::endl;
        return 35;
    } );

    // Wait for the promise to be fullfilled.   
    int thirtyFive = result->await(); // could also be async::await(result);

    std::cout << "Task has finished: " << thirtyFive << std::endl;
    
    



    return 0;
}