#pragma once
#include <atomic>
#include <functional>
#include <thread>

#include "utils/vec.hpp"

namespace foptim {

class JobSheduler;

enum class WorkerState : u8 {
  Running,
  Waiting,
};

struct Job {
  std::atomic<bool> *notifier = nullptr;
  std::function<void()> func = nullptr;
};

extern thread_local u8 worker_id;
struct Worker {
  std::atomic<WorkerState> state = WorkerState::Waiting;
  std::mutex job_queue;
  foptim::IRVec<Job> local_jobs;
  std::jthread thread;

  Worker(JobSheduler *shed, u8 id)
      : thread(&Worker::start_worker, this, shed, id) {}

  void start_worker(std::stop_token stoken, JobSheduler *shed, u8 id);
};

std::optional<Job> try_get_job(JobSheduler *shed, Worker *self);
void do_job(JobSheduler *shed, Worker *self = nullptr);

class JobSheduler {
 public:
  // this is definetly not optimal for short running jobs
  std::mutex job_queue;
  foptim::IRVec<Job> jobs;
  std::atomic<bool> new_work_there;

  Worker *threads;
  u8 n_threads = 0;

  void init(u8 n_threads) {
    ZoneScopedN("InitThreads");
    jobs.reserve(32);
    threads = (Worker *)malloc(sizeof(Worker) * n_threads);
    this->n_threads = n_threads;
    for (u8 i = 0; i < n_threads; i++) {
      new (&threads[i]) Worker{this, (u8)(i + 1)};
    }
    new_work_there.store(false, std::memory_order_release);
  }

  void deinit() {
    ZoneScopedN("DeinitThreads");
    new_work_there.store(true, std::memory_order_release);
    new_work_there.notify_all();
    for (u8 tid = 0; tid < n_threads; tid++) {
      threads[tid].thread.request_stop();
    }
    for (u8 tid = 0; tid < n_threads; tid++) {
      threads[tid].thread.join();
    }
    jobs.clear();
    free(threads);
  }

  void push(std::atomic<bool> *res, std::function<void()> j) {
    if (worker_id == 0) {
      push_base(Job{.notifier = res, .func = j});
    } else {
      push(&threads[worker_id], Job{.notifier = res, .func = j});
    }
  }

  void push_base(Job j) {
    if (!new_work_there.load(std::memory_order_relaxed)) {
      new_work_there.store(true, std::memory_order_release);
      new_work_there.notify_all();
    }
    std::lock_guard<std::mutex> queue(job_queue);
    jobs.push_back(std::move(j));
  }

  void push(Worker *worker, Job j) {
    if (!new_work_there.load(std::memory_order_relaxed)) {
      new_work_there.store(true, std::memory_order_release);
      new_work_there.notify_all();
    }
    std::lock_guard<std::mutex> queue(worker->job_queue);
    worker->local_jobs.push_back(std::move(j));
  }

  void wait_for(std::atomic<bool> *res, std::memory_order order) {
    while (!res->load(order)) {
      auto new_job = try_get_job(this, nullptr);
      if (!new_job.has_value()) {
        std::this_thread::yield();
        continue;
      }
      auto job = std::move(new_job.value());
      job.func();
      if (job.notifier != nullptr) {
        job.notifier->store(true, std::memory_order::release);
        job.notifier->notify_all();
      }
    }
  }

  void wait_till_done() {
    while (true) {
      auto new_job = try_get_job(this, nullptr);
      if (!new_job.has_value()) {
        bool all_threads_waiting = true;
        for (u8 tid = 0; tid < n_threads; tid++) {
          if (threads[tid].state.load(std::memory_order_acquire) !=
              WorkerState::Waiting) {
            all_threads_waiting = false;
            break;
          }
        }
        if (all_threads_waiting) {
          break;
        }
        std::this_thread::yield();
        continue;
      }
      auto job = std::move(new_job.value());
      job.func();
      if (job.notifier != nullptr) {
        job.notifier->store(true, std::memory_order::release);
        job.notifier->notify_all();
      }
    }
    new_work_there.store(false, std::memory_order_release);
  }
};

}  // namespace foptim
