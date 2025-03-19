#include "job_system.hpp"
#include <mutex>
#include <thread>

namespace foptim {

void Worker::work_func(JobSheduler *shed, std::stop_token stoken) {
  while (!stoken.stop_requested()) {
    if (shed->jobs.empty()) {
      state = WorkerState::Waiting;
      std::this_thread::yield();
      continue;
    }

    state = WorkerState::Running;
    Job job;
    {
      std::lock_guard<std::mutex> queue_gard{shed->job_queue};
      if (shed->jobs.empty()) {
        continue;
      }
      job = shed->jobs.back();
      shed->jobs.pop_back();
    }
    job.func();
  }
}

} // namespace foptim
