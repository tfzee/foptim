#pragma once
#include "utils/vec.hpp"
#include <functional>
#include <thread>

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
  WorkerState state = WorkerState::Waiting;
  std::jthread thread;

  Worker(JobSheduler *shed, u8 id)
      : worker_id(id), thread(&Worker::work_func, this, shed) {}

  void work_func(std::stop_token stoken, JobSheduler *shed);
};

class JobSheduler {
public:
  // this is definetly not optimal for short running jobs
  std::mutex job_queue;
  foptim::IRVec<Job> jobs;

  foptim::IRVec<Worker> threads;

  void init(u8 n_threads) {
    ZoneScopedN("InitThreads");
    jobs.reserve(32);
    threads.reserve(n_threads);
    for (u32 i = 0; i < n_threads; i++) {
      threads.emplace_back(this, i + 1);
    }
  }

  void deinit() {
    ZoneScopedN("DeinitThreads");
    for (auto &t : threads) {
      t.thread.request_stop();
    }
    for (auto &t : threads) {
      t.thread.join();
    }
    jobs.clear();
    threads.clear();
  }

  void push(std::function<void()> j) { push(Job{j}); }

  void push(Job j) {
    std::lock_guard<std::mutex> queue(job_queue);
    jobs.push_back(j);
  }

  void wait_till_done() {
    while (!jobs.empty()) {
      Job job;
      {
        std::lock_guard<std::mutex> queue_gard{job_queue};
        if (jobs.empty()) {
          // kinda problematic we yield while holding the lock
          //  but since jobs was empty here its unlikely to be full later
          //  atleast in my usecase
          std::this_thread::yield();
          continue;
        }
        job = jobs.back();
        jobs.pop_back();
      }
      job.func();
    }

    bool done = false;
    while (!done) {
      done = true;
      for (auto &thread : threads) {
        if (thread.state == WorkerState::Running) {
          done = false;
          std::this_thread::yield();
          break;
        }
      }
    }
  }
};

} // namespace foptim
