#include <atomic>
#include <mutex>
#include <thread>

#include "job_system.hpp"
#include "utils/arena.hpp"

namespace foptim {

thread_local u8 worker_id = 0;
thread_local char thread_name[11] = {'W', 'o', 'r', 'k', 'e', 'r',
                                     ' ', '0', '0', '0', 0};

void Worker::start_worker(std::stop_token, JobSheduler *shed, u8 id) {
  worker_id = id;
#ifdef TRACY_ENABLE
  thread_name[9] = '0' + worker_id % 10;
  thread_name[8] = '0' + (worker_id / 10) % 10;
  thread_name[7] = '0' + (worker_id / 100) % 10;
  tracy::SetThreadName(thread_name);
#endif
  while (!shed->stopping.load(std::memory_order_acquire)) {
    auto job = shed->try_get_job();
    if (job.has_value()) {
      shed->run_job(*job);
    } else {
      shed->sleep_until_work();
    }
  }

  foptim::utils::TempAlloc<void *>::free();
  foptim::utils::IRAlloc<void *>::free();
}

} // namespace foptim
