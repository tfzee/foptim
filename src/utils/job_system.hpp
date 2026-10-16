#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include "utils/vec.hpp"

namespace foptim {

class JobSheduler;

struct Job {
  std::atomic<bool> *notifier = nullptr;
  std::function<void()> func = nullptr;
};

extern thread_local u8 worker_id;
struct Worker {
  std::jthread thread;

  Worker(JobSheduler *shed, u8 id)
      : thread(&Worker::start_worker, this, shed, id) {}

  void start_worker(std::stop_token stoken, JobSheduler *shed, u8 id);
};

// One shared queue. Jobs are pushed in batches by the main thread, which
// helps to run them in wait_till_done. Idle workers spin briefly and then
// sleep until the next push.
class JobSheduler {
public:
  std::mutex job_queue;
  foptim::IRVec<Job> jobs;
  // number of jobs in the queue, lets idle threads check without locking
  std::atomic<u32> queued = 0;
  // jobs pushed and not finished yet
  std::atomic<u32> pending = 0;
  // changes on every push, sleeping workers wait for it to change
  std::atomic<u32> work_epoch = 0;
  std::atomic<u32> sleepers = 0;
  std::atomic<bool> stopping = false;

  Worker *threads = nullptr;
  u8 n_threads = 0;

  void init(u8 n_threads) {
    ZoneScopedN("InitThreads");
    jobs.reserve(32);
    threads = reinterpret_cast<Worker *>(malloc(sizeof(Worker) * n_threads));
    this->n_threads = n_threads;
    for (u8 i = 0; i < n_threads; i++) {
      new (&threads[i]) Worker{this, static_cast<u8>(i + 1)};
    }
  }

  void deinit() {
    ZoneScopedN("DeinitThreads");
    stopping.store(true, std::memory_order_seq_cst);
    work_epoch.fetch_add(1, std::memory_order_seq_cst);
    work_epoch.notify_all();
    for (u8 tid = 0; tid < n_threads; tid++) {
      threads[tid].thread.join();
    }
    jobs.clear();
    free(threads);
  }

  void push(std::atomic<bool> *res, std::function<void()> j) {
    pending.fetch_add(1, std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> queue(job_queue);
      jobs.push_back(Job{.notifier = res, .func = std::move(j)});
      queued.fetch_add(1, std::memory_order_seq_cst);
    }
    work_epoch.fetch_add(1, std::memory_order_seq_cst);
    if (sleepers.load(std::memory_order_seq_cst) != 0) {
      work_epoch.notify_one();
    }
  }

  // nullopt if the queue is empty
  std::optional<Job> try_get_job() {
    if (queued.load(std::memory_order_relaxed) == 0) {
      return {};
    }
    std::lock_guard<std::mutex> queue(job_queue);
    if (jobs.empty()) {
      return {};
    }
    auto job = std::move(jobs.back());
    jobs.pop_back();
    queued.fetch_sub(1, std::memory_order_relaxed);
    return job;
  }

  void run_job(Job &job) {
    job.func();
    if (job.notifier != nullptr) {
      job.notifier->store(true, std::memory_order::release);
      job.notifier->notify_all();
    }
    if (pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      pending.notify_all();
    }
  }

  // runs jobs until the queue is empty, then waits for the running ones
  void wait_till_done() {
    while (true) {
      auto new_job = try_get_job();
      if (new_job.has_value()) {
        run_job(*new_job);
        continue;
      }
      const u32 p = pending.load(std::memory_order_acquire);
      if (p == 0) {
        break;
      }
      pending.wait(p, std::memory_order_acquire);
    }
  }

  // runs other jobs until the given one finished
  void wait_for(std::atomic<bool> *res, std::memory_order order) {
    while (!res->load(order)) {
      auto new_job = try_get_job();
      if (!new_job.has_value()) {
        std::this_thread::yield();
        continue;
      }
      run_job(*new_job);
    }
  }

  // blocks a worker until there is work or the scheduler stops
  void sleep_until_work() {
    // spinning first is cheaper than a futex round trip for back to back
    // batches
    for (u32 i = 0; i < 2000; i++) {
      if (queued.load(std::memory_order_relaxed) != 0 ||
          stopping.load(std::memory_order_relaxed)) {
        return;
      }
      __builtin_ia32_pause();
    }
    sleepers.fetch_add(1, std::memory_order_seq_cst);
    const u32 epoch = work_epoch.load(std::memory_order_seq_cst);
    if (queued.load(std::memory_order_seq_cst) == 0 &&
        !stopping.load(std::memory_order_seq_cst)) {
      work_epoch.wait(epoch, std::memory_order_seq_cst);
    }
    sleepers.fetch_sub(1, std::memory_order_relaxed);
  }
};

} // namespace foptim
