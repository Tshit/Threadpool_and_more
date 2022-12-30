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
    //����һ��ָ���̸߳�����thread pool����
    explicit ThreadPool(std::size_t num_threads) {
        progress_trackers_.resize(num_threads);
        // ���������߳�
        for (std::size_t i = 0; i < num_threads; i++) {
            worker_threads_[i] = std::thread([this, i] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        // �ȴ�������û�ر�
                        cv_.wait(lock, [this] { return !tasks_.empty() || stop_;  });
                        if (stop_) {
                            // ���ⲿ����stop_���ʱ�ر�
                            return;
                        }
                        if (tasks_.empty()) {
                            // ������Ϊ��ʱ�ȴ�����
                            continue;
                        }
                        // �Ӷ�����ȡ������
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    // ִ������
                    task();
                }
                });
        }
    }
 
    // ��������
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        Stop();
    }
    // ��һ��������ӽ�����. ����std::future�����������ķ��ؽ��
    template <typename F>
    std::future<typename std::result_of<F()>::type> AddTask(F&& task , size_t thread_index) {
        // ����һ��packaged_task���ڰ�װ����
        std::packaged_task<typename std::result_of<F()>::type()> pt(std::forward<F>(task));
        // ��ȡ��packaged_task��ص�future
        std::future<typename std::result_of<F()>::type> future = pt.get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // ��packaged_task��ӽ�����.
            tasks_.emplace([pt = std::move(pt)]() mutable { pt(); });
        }
        // ֪ͨ�����߳���һ���������
        cv_.notify_one();
        return future;
    }

    // ȡ�������е���������
    void CancelAllTasks() {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_ = {};
    }

    // ���ô˺�����ֹͣ����������¼��ǰλ��
    void Stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        
        // ��ProgressTracker�����м�¼��ǰ�ϴ���
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
    // ProgressTracker���캯��
    ProgressTracker(const std::string& name, std::size_t size)
        : name_(name), size_(size) {}

    // ����ProgressTracker�����Ʊ�ʶ
    const std::string& GetName() const { return name_; }

    // �����ܴ�С
    std::size_t GetSize() const { return size_; }

    // ���ص�ǰλ��
    std::size_t GetPosition() const { return current_position_; }

    // ���µ�ǰλ��
    void UpdatePosition(std::size_t position) { current_position_ = position; }

    // ��¼��ǰλ��
    void RecordPosition() {
        std::lock_guard<std::mutex> lock(mutex_);
        recorded_position_ = current_position_;
        auto thread_name = name_;
        //���߳���,��¼λ��д����ʱ�ļ�(json?)
    }

    // ��ȡ��¼��λ��.
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
    // ����һ��ThreadPool������4�������߳�
    ThreadPool thread_pool(4);

    // Ϊÿһ�������̴߳���һ��ProgressTracker
    std::vector<ProgressTracker> progress_trackers(4);

    // ���ϴ���������̳߳�
    for (std::size_t i = 0; i < 4; ++i) {
        thread_pool.AddTask([&progress_trackers, i] {
            // Download a chunk of the file.
            std::size_t bytes_transferred = UploadChunk(i);

        // Update the ProgressTracker for this worker thread.
        progress_trackers[i].UpdatePosition(bytes_transferred);
            }, i);
    }

    // �ȴ��������
    thread_pool.Stop();

    // �������ϴ���
    std::size_t total_bytes_transferred = 0;
    for (std::size_t i = 0; i < 4; i++) {
        total_bytes_transferred += progress_trackers[i].GetRecordedPosition();
    }

    std::cout << "���ϴ���: " << total_bytes_transferred << std::endl;

    return 0;
}
