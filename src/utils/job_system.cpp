#include <atomic>
#include <mutex>
#include <thread>

#include "job_system.hpp"
#include "utils/arena.hpp"

namespace foptim {

thread_local u8 worker_id = 0;
thread_local char thread_name[11] = {'W', 'o', 'r', 'k', 'e', 'r',
                                     ' ', '0', '0', '0', 0};

std::optional<Job> try_get_job(JobSheduler *shed, Worker *self) {
  if (self != nullptr) {
    std::lock_guard<std::mutex> queue_gard{self->job_queue};
    if (!self->local_jobs.empty()) {
      if (self != nullptr) {
        self->state.store(WorkerState::Running, std::memory_order_release);
      }
      auto job = std::move(self->local_jobs.back());
      self->local_jobs.pop_back();
      return job;
    }
  }
  {
    std::lock_guard<std::mutex> queue_gard{shed->job_queue};
    if (!shed->jobs.empty()) {
      if (self != nullptr) {
        self->state.store(WorkerState::Running, std::memory_order_release);
      }
      auto job = std::move(shed->jobs.back());
      shed->jobs.pop_back();
      return job;
    }
  }
  {
    for (u8 other_thread_id = 0; other_thread_id < shed->n_threads;
         other_thread_id++) {
      if ((self != nullptr) && (other_thread_id + 1) == worker_id) {
        continue;
      }
      auto &other_thread = shed->threads[other_thread_id];
      std::lock_guard<std::mutex> queue_gard{other_thread.job_queue};
      if (!other_thread.local_jobs.empty()) {
        if (self != nullptr) {
          self->state.store(WorkerState::Running, std::memory_order_release);
        }
        auto job = std::move(other_thread.local_jobs.back());
        other_thread.local_jobs.pop_back();
        return job;
      }
    }
  }
  return {};
}

void do_job(JobSheduler *shed, Worker *self) {
  auto new_job = try_get_job(shed, self);
  if (!new_job.has_value()) {
    self->state = WorkerState::Waiting;
    std::this_thread::yield();
    return;
  }
  auto job = std::move(new_job.value());
  job.func();
  if (job.notifier != nullptr) {
    job.notifier->store(true, std::memory_order::release);
    job.notifier->notify_all();
  }
}

void Worker::start_worker(std::stop_token stoken, JobSheduler *shed, u8 id) {
  worker_id = id;
#ifdef TRACY_ENABLE
  thread_name[9] = '0' + worker_id % 10;
  thread_name[8] = '0' + (worker_id / 10) % 10;
  thread_name[7] = '0' + (worker_id / 100) % 10;
  tracy::SetThreadName(thread_name);
#endif
  while (!stoken.stop_requested()) {
    if (state.load(std::memory_order::relaxed) == WorkerState::Waiting) {
      std::this_thread::yield();
      shed->new_work_there.wait(false, std::memory_order_acquire);
    }
    do_job(shed, this);
  }

  foptim::utils::TempAlloc<void *>::free();
  foptim::utils::IRAlloc<void *>::free();
}

}  // namespace foptim
