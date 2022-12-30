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
        // 创建工作线程
        for (std::size_t i = 0; i < num_threads; i++) {
            worker_threads_[i] = std::thread([this, i] {
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
    std::future<typename std::result_of<F()>::type> AddTask(F&& task , size_t thread_index) {
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
        
        // 在ProgressTracker对象中记录当前上传量
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
        auto thread_name = name_;
        //将线程名,记录位置写入临时文件(json?)
    }

    // 获取记录的位置.
    std::size_t GetRecordedPosition() {
        std::lock_guard<std::mutex> lock(mutex_);
        return recorded_position_;
    }

private:
    std::string name_;
    std::size_t size_;
    std::size_t current_position_ = 0;
    std::size_t recorded_position_ = 0;
    std::mutex mutex_;
};

int main() {
    // 创建一个ThreadPool对象，有4个工作线程
    ThreadPool thread_pool(4);

    // 为每一个工作线程创建一个ProgressTracker
    std::vector<ProgressTracker> progress_trackers(4);

    // 将上传任务加入线程池
    for (std::size_t i = 0; i < 4; ++i) {
        thread_pool.AddTask([&progress_trackers, i] {
            // Download a chunk of the file.
            std::size_t bytes_transferred = UploadChunk(i);

        // Update the ProgressTracker for this worker thread.
        progress_trackers[i].UpdatePosition(bytes_transferred);
            }, i);
    }

    // 等待任务结束
    thread_pool.Stop();

    // 计算总上传量
    std::size_t total_bytes_transferred = 0;
    for (std::size_t i = 0; i < 4; i++) {
        total_bytes_transferred += progress_trackers[i].GetRecordedPosition();
    }

    std::cout << "总上传量: " << total_bytes_transferred << std::endl;

    return 0;
}
