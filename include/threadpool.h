#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <iostream>

class ThreadPool {
public:
    ThreadPool(size_t threads = 0) : stop(false) {
        // OPTIMIZATION: Reserve 1 core for the Main/Render thread. 
        // Using all cores often causes the game loop to stutter.
        if (threads == 0) {
            unsigned int hw = std::thread::hardware_concurrency();
            if (hw > 2) threads = hw - 2;
            else threads = 1;
        }

        std::cout << "[System] Initializing ThreadPool with " << threads << " workers." << std::endl;

        for(size_t i = 0; i < threads; ++i)
            workers.emplace_back(
                [this] {
                    for(;;) {
                        std::function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(this->queue_mutex);
                            this->condition.wait(lock, [this]{ return this->stop || !this->tasks.empty(); });
                            
                            // SHUTDOWN FIX: 
                            // If stop is true, exit immediately. Do not process the remaining queue.
                            // This prevents the "Hang on Exit" bug.
                            if(this->stop) 
                                return;
                                
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }

                        task();
                    }
                }
            );
    }

    // Enqueue a generic void function/lambda
    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop) return; // Don't accept work if stopping
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
            
            // CRITICAL FIX: Clear the queue. 
            // We swap with an empty queue to discard all pending generation tasks immediately.
            std::queue<std::function<void()>> empty;
            std::swap(tasks, empty);
        }
        
        condition.notify_all();
        
        for(std::thread &worker: workers) {
            if(worker.joinable())
                worker.join();
        }
    }

    size_t GetWorkerCount() const { return workers.size(); }
    size_t GetQueueSize() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        return tasks.size();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};