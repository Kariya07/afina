#include <afina/concurrency/Executor.h>

#include <algorithm>
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
    std::unique_lock<std::mutex> lock(mutex);
    state = State::kRun;
    size_t iter = 0;
    free_threads = 0;
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
    bool time_update = true, loop_exit = false;
    std::function<void()> task;
    auto current_time = std::chrono::system_clock::now();
    while (executor->state == Executor::State::kRun) {
        if (time_update) {
            current_time = std::chrono::system_clock::now();
        }
        {
            std::unique_lock<std::mutex> _lock(executor->mutex);
            while ((executor->state == Executor::State::kRun) && executor->tasks.empty()) {
                executor->free_threads++;
                if (executor->empty_condition.wait_until(_lock, current_time +
                                                                    std::chrono::milliseconds(executor->idle_time)) ==
                    std::cv_status::timeout) {
                    if (executor->threads.size() == executor->low_watermark) {
                        executor->empty_condition.wait(_lock);
                    } else {
                        loop_exit = true;
                        break;
                    }
                }
                executor->free_threads--;
            }
            if (loop_exit) {
                break;
            }
            if (executor->tasks.empty()) {
                time_update = false;
                continue;
            }

            task = executor->tasks.front();
            executor->tasks.pop_front();
        }
        try {
            task();
        } catch (std::runtime_error &ex) {
            std::cout << "Error in task: " << ex.what() << std::endl;
        }
        time_update = true;
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
    std::thread::id pid = std::this_thread::get_id();

    auto it = threads.begin();
    for(; it != threads.end(); ++it){
        if(it->get_id() == pid){
            break;
        }
    }
    if (it != threads.end()) {
        it->detach();
        free_threads--;
        threads.erase(it);
    } else {
        std::cout << "Failed to delete thread: " << pid << std::endl;
    }
}
} // namespace Concurrency
} // namespace Afina
