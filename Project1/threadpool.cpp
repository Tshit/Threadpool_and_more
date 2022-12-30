#include <iostream>
#include <fstream>
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

class ThreadPool {
 public:
    //构建一个指定线程个数的thread pool对象
    explicit ThreadPool(std::size_t num_threads) {
        progress_trackers_.resize(num_threads);
        // Create the worker threads.
        for (std::size_t i = 0; i < num_threads; i++) {
            worker_threads_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        // 等待任务可用或关闭
                        cv_.wait(lock, [this] { return !tasks_.empty() || stop_;  });
                        if (stop_) {
                            // 当外部设置stop_标记时关闭
                            return;
                        }
                        if (tasks_.empty()) {
                            // 当队列为空时等待任务
                            continue;
                        }
                        // 从队列中取出任务
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    // 执行任务
                    task();
                }
                });
        }
    }
 
    // 析构函数
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        Stop();
    }
    // 将一个任务添加进队列. 返回std::future用来存放任务的返回结果
    template <typename F>
    std::future<typename std::result_of<F()>::type> AddTask(F&& task) {
        // 创建一个packaged_task用于包装任务
        std::packaged_task<typename std::result_of<F()>::type()> pt(std::forward<F>(task));
        // 获取与packaged_task相关的future
        std::future<typename std::result_of<F()>::type> future = pt.get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // 将packaged_task添加进队列.
            tasks_.emplace([pt = std::move(pt)]() mutable { pt(); });
        }
        // 通知工作线程有一个任务可用
        cv_.notify_one();
        return future;
    }

    // 取消队列中的所有任务
    void CancelAllTasks() {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_ = {};
    }

    // 调用此函数以停止工作，并记录当前位置
    void Stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        
        // Record the current position in the ProgressTracker objects.
        for (std::size_t i = 0; i < progress_trackers_.size(); ++i) {
            progress_trackers_[i].RecordPosition();
        }
    }

private:
    std::vector<std::thread> worker_threads_;
    std::queue<std::function<void()>> tasks_;
    std::vector<ProgressTracker> progress_trackers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};


class ProgressTracker {
public:
    // ProgressTracker构造函数
    ProgressTracker(const std::string& name, std::size_t size)
        : name_(name), size_(size) {}

    // 返回ProgressTracker的名称标识
    const std::string& GetName() const { return name_; }

    // 返回总大小
    std::size_t GetSize() const { return size_; }

    // 返回当前位置
    std::size_t GetPosition() const { return current_position_; }

    // 更新当前位置
    void UpdatePosition(std::size_t position) { current_position_ = position; }

    // 记录当前位置
    void RecordPosition() {
        std::lock_guard<std::mutex> lock(mutex_);
        recorded_position_ = current_position_;
    }

private:
    std::string name_;
    std::size_t size_;
    std::size_t current_position_ = 0;
    std::size_t recorded_position_ = 0;
    std::mutex mutex_;
};

int main() {
    // Create a thread pool with 4 worker threads.
    ThreadPool thread_pool(4);

    // Determine the size of the file to download.
    std::size_t size = 0;  // Replace this with code to determine the size of the file.
    ProgressTracker progress_tracker("thread1", size);

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
                std::size_t total_size = progress_tracker.GetSize();
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
}
