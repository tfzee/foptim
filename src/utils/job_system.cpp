#include "job_system.hpp"
#include <mutex>
#include <thread>

namespace foptim {

void Worker::work_func(JobSheduler &shed) {
  while (true) {
    if (shed.jobs.empty()) {
      std::this_thread::yield();
    }

    Job job;
    {
      std::lock_guard<std::mutex> queue_gard{shed.job_queue};
      if (shed.jobs.empty()) {
        continue;
      }
      job = shed.jobs.back();
      shed.jobs.pop_back();
    }
    job.func(job.arg);
  }
}

} // namespace foptim
