#pragma once
#include "utils/todo.hpp"
#include "utils/vec.hpp"
#include <common/TracySystem.hpp>
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
      : worker_id(id), thread([this, shed](std::stop_token stoken) {
          work_func(shed, stoken);
        }) {}

  void work_func(JobSheduler *shed, std::stop_token stoken);
};

class JobSheduler {
public:
  // this is definetly not optimal for short running jobs
  std::mutex job_queue;
  foptim::IRVec<Job> jobs;

  foptim::IRVec<Worker> threads;

  void init(u8 n_threads) {
    jobs.reserve(10);
    threads.reserve(n_threads);
    for (u32 i = 0; i < n_threads; i++) {
      threads.emplace_back(this, i + 1);
    }
  }
  void deinit() {
    for (auto &t : threads) {
      t.thread.request_stop();
    }
    for (auto &t : threads) {
      t.thread.join();
    }
    jobs.clear();
    threads.clear();
  }

  ~JobSheduler() {}

  void push(std::function<void()> j) { push(Job{j}); }

  void push(Job j) {
    std::lock_guard<std::mutex> queue(job_queue);
    jobs.push_back(j);
  }

  void wait_till_done() {
    bool done = false;
    while (!jobs.empty()) {
      std::this_thread::yield();
    }
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
