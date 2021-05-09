# CPPASYNC

cppasync is a lightweight, header-only c++ (20) library that provides a framework for fast
asynchronous operations. The main advantage of cppasync over c++'s built in asynchronous, future-based
functions is that the EventLoop class will internally manage a threadpool. This will make the costs of initiating
a new coroutine negligable as compared to initiating a coroutine through c++'s future, which has to 
initiate a new thread every time.

# Installation

cppasync is a header-only library, so just include 'async.hpp' in your project and don't forget to 
link with '-lpthread' on unix-based systems.

### Note: cppasync requires c++20 or above.

# License

cppasync is licensed under the MIT license.

# Example Hello World

```c++
#include "async.hpp"
#include <iostream>
int main(){
    
    // Create an event loop to async tasks
    async::EventLoop loop;

    // execute a task, and get the promise
    async::Promise<int> result = loop.execute<int>( [](){
        std::cout << "This is running async." << std::endl;
        return 35;
    } );

    // Wait for the promise to be fullfilled.  
    // This statement could also be done with async::await(result); 
    int thirtyFive = result->await(); 

    // The return value from the function is now in our 'thirtyFive' variable now that we have awaited the promise
    std::cout << "Task has finished: " << thirtyFive << std::endl;
    
    
    return 0;
}
```

# Ways to execute

You can execute a 'coroutine' (A function that runs asynchronously) through a number of different methods with cpp async.

## Basic Promise-based task

The most simple way to execute a task would be to use ```EventLoop::execute<t>``` with ```t``` being the returntype. This will return a ```Promise``` object, which you MUST store and await to get the result. If you do not await a promise or do not store a promise, it might be deallocated. This causes undefined behavior (usually a SIGSEGV).

## Coroutine object

To call the same coroutine a number of times, you can use a ```Coroutine``` object. Example:

```cpp
async::Coroutine c;
c.setfn(puts, "Hello World");
auto promise = eventloop.execute(c);
```

## Launch

In situations where you do not care about the results or completion of a task you are running, you can use the ```launch``` variant of the other ```execute``` functions. Calling launch on a coroutine will run it through the threadpool, without taking the return value, and without returning any promise. You will have no built-in way of know if it has finished, but for tasks where that is not a problem ```launch``` is ideal.
Example:

```cpp
eventloop.launch(puts, "HelloWorld!");
```

