#ifndef THREAD_SEMAPHORE_HPP
#define THREAD_SEMAPHORE_HPP

// Code blatantly stolen from 'Doug' at stackoverflow.com

#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>

class semaphore
{
    //The current semaphore count.
    unsigned int count;

    //mutex protects count.
    //Any code that reads or writes the count data must hold a lock on the mutex.
    boost::mutex mutex;

    //Code that increments count must notify the condition variable.
    boost::condition_variable condition;

public:
    explicit semaphore(unsigned int initial_count = 0) : count(initial_count), mutex(),  condition()
    {
    }

	/*
    unsigned int get_count() const //for debugging/testing only
    {
        //The "lock" object locks the mutex when it's constructed,
        //and unlocks it when it's destroyed.
        boost::unique_lock<boost::mutex> lock(mutex);
        return count;
    }
	*/

    void signal() //called "release" in Java
    {
        boost::unique_lock<boost::mutex> lock(mutex);
        count++;
        //Wake up any waiting threads. 
        //Always do this, even if count_ wasn't 0 on entry. 
        //Otherwise, we might not wake up enough waiting threads if we 
        //get a number of signal() calls in a row.
        condition.notify_one(); 
    }

    void wait() //called "acquire" in Java
    {
        boost::unique_lock<boost::mutex> lock(mutex);
        while(count == 0)
        {
             condition.wait(lock);
        }
        count--;
    }

};

#endif
