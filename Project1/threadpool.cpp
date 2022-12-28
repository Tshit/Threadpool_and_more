#include <iostream>
#include <functional>
#include <future>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstddef>
#include <type_traits>

// A simple thread pool class.
class ThreadPool {
 public:
    // Constructs a thread pool with the given number of worker threads.
    explicit ThreadPool(std::size_t num_threads) {
        // Create the worker threads.
        for (std::size_t i = 0; i < num_threads; i++) {
            worker_threads_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        // Wait until there is a task available or the thread pool is shutting down.
                        cv_.wait(lock, [this] { return !tasks_.empty() || stop_; });
                        if (stop_ && tasks_.empty()) {
                            // Stop the worker thread if the thread pool is shutting down and there are no more tasks.
                            return;
                        }
                        // Get the next task from the queue.
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    // Execute the task.
                    task();
                }
                });
        }
    }
 
    // Destroys the thread pool.
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& worker : worker_threads_) {
            worker.join();
        }
    }
    // Adds a task to the queue. Returns a std::future that can be used to retrieve the result of the task.
    template <typename F>
    std::future<typename std::result_of<F()>::type> AddTask(F&& task) {
        // Create a packaged_task to wrap the given task.
        std::packaged_task<typename std::result_of<F()>::type()> pt(std::forward<F>(task));
        // Get the future associated with the packaged_task.
        std::future<typename std::result_of<F()>::type> future = pt.get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Add the packaged_task to the queue.
            tasks_.emplace([pt = std::move(pt)]() mutable { pt(); });
        }
        // Notify a worker thread that there is a task available.
        cv_.notify_one();
        return future;
    }

private:
    std::vector<std::thread> worker_threads_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};


class ProgressTracker {
public:
    // Constructs a progress tracker with the given total size.
    explicit ProgressTracker(std::size_t total_size) : total_size_(total_size) {}

    // Returns the current position of the operation.
    std::size_t GetPosition() const { return position_.load(); }

    // Returns the total size of the operation.
    std::size_t GetTotalSize() const { return total_size_; }

    // Increments the current position by the given amount.
    void UpdatePosition(std::size_t increment) { position_ += increment; }

private:
    std::atomic<std::size_t> position_ = 0;
    std::size_t total_size_;
};

int main() {
    // Create a thread pool with 4 worker threads.
    ThreadPool thread_pool(4);

    // Determine the size of the file to download.
    std::size_t size = 0;  // Replace this with code to determine the size of the file.
    ProgressTracker progress_tracker(size);

    // Download the file in blocks of 4KB.
    constexpr std::size_t block_size = 4 * 1024;
    std::vector<std::future<std::vector<char>>> futures;
    std::size_t position = 0;
    while (position < size) {
        std::size_t download_size = std::min(block_size, size - position);
        futures.push_back(thread_pool.AddTask([&position, download_size, &progress_tracker] {
            // Download the block of data here...
            std::this_thread::sleep_for(std::chrono::milliseconds(500));  // Simulate download time
        position += download_size;  // Update the position
        progress_tracker.UpdatePosition(download_size);  // Update the progress tracker
        return std::vector<char>(download_size);
            }));
    }

    // Wait for the tasks to complete and write the downloaded data to the output file.
    std::ofstream output("output.txt", std::ios::binary);
    if (!output) {
        std::cerr << "Error: failed to open output file." << std::endl;
        return EXIT_FAILURE;
    }
    while (!futures.empty()) {

       // Wait for the tasks to completeand write the downloaded data to the output file.
        std::ofstream output("output.txt", std::ios::binary);
        if (!output) {
            std::cerr << "Error: failed to open output file." << std::endl;
            return EXIT_FAILURE;
        }
        while (!futures.empty()) {
            // Wait for any of the tasks to complete.
            auto it = std::find_if(futures.begin(), futures.end(), [](const auto& future) { return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
            if (it == futures.end()) {
                // Sleep for a short time and print the progress.
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::size_t position = progress_tracker.GetPosition();
                std::size_t total_size = progress_tracker.GetTotalSize();
                std::cout << "Progress: " << position << " / " << total_size << " (" << static_cast<double>(position) / total_size * 100 << "%)" << std::endl;
                continue;
            }
            // Get the result of the completed task and write it to the output file.
            std::vector<char> block = it->get();
            output.write(block.data(), block.size());
            futures.erase(it);
        }

        return 0;
    }
