#pragma once
#include "utils/todo.hpp"
#include "utils/vec.hpp"
#include <thread>

namespace foptim {
class JobSheduler;

enum class WorkerState : u8 {
  Running,
  Waiting,
};

struct Job {
  void* arg;
  void (*func)(void *) = nullptr;
};

struct Worker {
  WorkerState state = WorkerState::Waiting;
  std::jthread thread;

  void work_func(JobSheduler &shed);
};

class JobSheduler {
public:
  // this is definetly not optimal for short running jobs
  std::mutex job_queue;
  foptim::IRVec<Job> jobs;

  foptim::IRVec<Worker> threads;

  JobSheduler(u8 n_threads) {
    jobs.reserve(10);
    threads.resize(n_threads);
  }

  ~JobSheduler() {
    for (auto &worker : threads) {
      ASSERT(worker.thread.request_stop());
    }
  }

  void push(Job j) {
    std::lock_guard<std::mutex> queue(job_queue);
    jobs.push_back(j);
  }

  void wait_till_done() {
    while (!jobs.empty()) {
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
