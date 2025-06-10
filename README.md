# CPPASYNC

cppasync is a lightweight, header-only c++ (20) library that provides a framework for fast
asynchronous operations. The EventLoop class manages a set of tasks that will be executed asynchronously
on one or more threads. These tasks can be set to run at a specific time, or as soon as possible. Easy, familiar
semantics are available to simplify code whithout sacrificing efficiency.

# Installation

cppasync is a header-only library, so just include 'async.hpp' in your project and don't forget to 
link with '-lpthread' on unix-based systems.

### Note: cppasync requires c++20 or above.

# License

cppasync is licensed under the MIT license.

# Example Hello World

```c++
#include "async.hpp"

int main() {

    // Initialize an event loop with 3 threads (arbitrary number)
    async::EventLoop loop{3};

    // Run this task as soon as possible
    loop.run([](){
        printf("Hello World! (1)\n");
        return 5;
    });

    // Run this task also as soon as possible
    loop.run([](){
        printf("Hello World! (2)\n");
    });


    // Schedule "Hello World! (3)" to run in 1s,
    // and once that is complete pass the return 
    // value to the following task ("its over now :)")
    // to add 3.5 for a final result.
    auto test = loop.run_later(1000, [](){
        printf("Hello World! (3)\n");
        return 5;
    }).then([](int a){
        printf("its over now :)\n");
        return 3.5+a;
    });


    // Await the final result from the chained future,
    // which should be 3.5 + 5
    double result = test.get();

    // at this point all futures have been awaited
    printf("Final result: %g\n", result);

    // The EventLoop destructor will re-join all threads before exit
    return 0;
}
```

