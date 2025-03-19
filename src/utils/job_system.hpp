#pragma once
#include "utils/todo.hpp"
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
  WorkerState state = WorkerState::Waiting;
  std::jthread thread;

  Worker(JobSheduler *shed) {
    thread = std::jthread(
        [this, shed](std::stop_token stoken) { work_func(shed, stoken); });
  }

  void work_func(JobSheduler *shed, std::stop_token stoken);
};

class JobSheduler {
public:
  // this is definetly not optimal for short running jobs
  std::mutex job_queue;
  foptim::IRVec<Job> jobs;

  foptim::IRVec<Worker> threads;

  JobSheduler(u8 n_threads) {
    jobs.reserve(10);
    threads.reserve(n_threads);
    for (u32 i = 0; i < n_threads; i++) {
      threads.emplace_back(this);
    }
  }

  ~JobSheduler() {
    // wait_till_done();
    for (auto &worker : threads) {
      worker.thread.request_stop();
    }
  }

  void push(std::function<void()> j) { push(Job{j}); }

  void push(Job j) {
    std::lock_guard<std::mutex> queue(job_queue);
    jobs.push_back(j);
  }

  void wait_till_done() {
    // while (!jobs.empty()) {
    // }

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
