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
  std::function<void()> func = nullptr;
};

struct Worker {
  u8 worker_id;
  std::atomic<WorkerState> state = WorkerState::Waiting;
  std::jthread thread;

  Worker(JobSheduler *shed = nullptr, u8 id = 0)
      : worker_id(id), thread(&Worker::work_func, this, shed) {}

  void work_func(std::stop_token stoken, JobSheduler *shed);
};

class JobSheduler {
 public:
  // this is definetly not optimal for short running jobs
  std::mutex job_queue;
  foptim::IRVec<Job> jobs;

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
  }

  void deinit() {
    ZoneScopedN("DeinitThreads");
    for (u8 tid = 0; tid < n_threads; tid++) {
      threads[tid].thread.request_stop();
    }
    for (u8 tid = 0; tid < n_threads; tid++) {
      threads[tid].thread.join();
    }
    jobs.clear();
    free(threads);
  }

  void push(std::function<void()> j) { push(Job{j}); }

  void push(Job j) {
    std::lock_guard<std::mutex> queue(job_queue);
    jobs.push_back(j);
  }

  void wait_till_done() {
    while (true) {
      Job job;
      {
        std::lock_guard<std::mutex> queue_gard{job_queue};
        if (jobs.empty()) {
          break;
        }
        job = jobs.back();
        jobs.pop_back();
      }
      job.func();
    }

    bool done = false;
    while (!done) {
      done = true;
      for (u8 tid = 0; tid < n_threads; tid++) {
        if (threads[tid].state.load(std::memory_order::acquire) ==
            WorkerState::Running) {
          done = false;
          std::this_thread::yield();
          break;
        }
      }
    }
  }
};

}  // namespace foptim
