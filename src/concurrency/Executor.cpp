#include <afina/concurrency/Executor.h>

#include <iostream>
#include <utility>

namespace Afina {
namespace Concurrency {
Executor::Executor(int low_watermark, int hight_watermark, int max_queue_size, int idle_time)
    : low_watermark(low_watermark), hight_watermark(hight_watermark), max_queue_size(max_queue_size),
      idle_time(idle_time) {}
Executor::~Executor() {}

void Executor::Start(std::shared_ptr<spdlog::logger> logger) {
    _logger = std::move(logger);
    mutex.lock();
    state = State::kRun;
    mutex.unlock();
    size_t iter = 0;
    num_of_workers = low_watermark;
    while (iter < low_watermark) {
        threads.emplace_back(&perform, this);
        iter++;
    }
}

void Executor::Stop(bool await) {
    if (state == State::kStopped) {
        return;
    } else {
        std::unique_lock<std::mutex> lock(mutex);
        state = State::kStopping;
        empty_condition.notify_all();
        if (await) {
            while (state != State::kStopped) {
                server_stop_condition.wait(lock);
            }
        }
    }
}

void perform(Executor *executor) {
    std::function<void()> task;
    while (executor->state == Executor::State::kRun) {
        auto current_time = std::chrono::system_clock::now();
        {
            std::unique_lock<std::mutex> _lock(executor->mutex);
            while ((executor->state == Executor::State::kRun) && executor->tasks.empty()) {
                executor->num_of_workers--;
                if (executor->empty_condition.wait_until(_lock, current_time +
                                                                    std::chrono::milliseconds(executor->idle_time)) ==
                    std::cv_status::timeout) {
                    if (executor->threads.size() == executor->low_watermark) {
                        executor->empty_condition.wait(_lock);
                    } else {
                        executor->kill_thread();
                        return;
                    }
                }
                executor->num_of_workers++;
            }
            if (executor->tasks.empty()) {
                continue;
            }

            task = executor->tasks.front();
            executor->tasks.pop_front();
        }
        executor->num_of_workers++;
        task();
    }
    std::unique_lock<std::mutex> _lock(executor->mutex);
    executor->kill_thread();
    if (executor->threads.empty()) {
        executor->state = Executor::State::kStopped;
        executor->server_stop_condition.notify_all();
    }
}

void Executor::kill_thread() {
    // find thread with pid
    const std::thread::id pid = std::this_thread::get_id();
    bool flag = false;
    auto it = threads.begin();
    while ((it != threads.end()) && (!flag)) {
        flag = (pid == it->get_id());
    }
    if (flag) {
        it->detach();
        threads.erase(it);
    } else {
        throw std::runtime_error("Out of range in Threads");
    }
}
} // namespace Concurrency
} // namespace Afina
