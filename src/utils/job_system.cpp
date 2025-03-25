#include "job_system.hpp"
#include <mutex>
#include <thread>

namespace foptim {

thread_local char thread_name[11] = {'W', 'o', 'r', 'k', 'e', 'r',
                                     ' ', '0', '0', '0', 0};

void Worker::work_func(JobSheduler *shed, std::stop_token stoken) {
  thread_name[9] = '0' + worker_id % 10;
  thread_name[8] = '0' + (worker_id / 10) % 10;
  thread_name[7] = '0' + (worker_id / 100) % 10;
  tracy::SetThreadName(thread_name);
  while (!stoken.stop_requested()) {
    if (shed->jobs.empty()) {
      std::this_thread::yield();
      continue;
    }

    state = WorkerState::Running;
    Job job;
    {
      std::lock_guard<std::mutex> queue_gard{shed->job_queue};
      if (shed->jobs.empty()) {
        state = WorkerState::Waiting;
        continue;
      }
      job = shed->jobs.back();
      shed->jobs.pop_back();
    }
    job.func();
    state = WorkerState::Waiting;
  }

  foptim::utils::TempAlloc<void *>::free();
}

} // namespace foptim
