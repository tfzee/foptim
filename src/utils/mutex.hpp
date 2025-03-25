#pragma once
#include <client/TracyLock.hpp>
#include <shared_mutex>

namespace foptim {

template <class T> class Mutex;

template <class T> class MutMutexGuard {
  Mutex<T> *const mutex;

public:
  MutMutexGuard(Mutex<T> *cont) : mutex(cont) { mutex->_mutex.lock(); }
  ~MutMutexGuard() { mutex->_mutex.unlock(); }
  T *operator->() { return &mutex->_contained; }
};

template <class T> class ConstMutexGuard {
  Mutex<T> *const mutex;

public:
  ConstMutexGuard(Mutex<T> *cont) : mutex(cont) { mutex->_mutex.lock_shared(); }
  ~ConstMutexGuard() { mutex->_mutex.unlock_shared(); }
  const T *operator->() { return &mutex->_contained; }
};

template <class T> class Mutex {
public:
  T _contained;
#ifndef TRACY_ENABLE
  std::shared_mutex _mutex;
#else
  tracy::SharedLockable<std::shared_mutex> _mutex = {
      []() -> const tracy::SourceLocationData * {
        static constexpr tracy::SourceLocationData srcloc{
            nullptr, "std::shared_mutex wrapper", __FILE__, __LINE__, 0};
        return &srcloc;
      }()};
#endif

  [[nodiscard]] MutMutexGuard<T> scoped_lock() const {
    return MutMutexGuard<T>{const_cast<Mutex<T> *>(this)};
  }

  [[nodiscard]] MutMutexGuard<T> scoped_lock() {
    return MutMutexGuard<T>{this};
  }
};

} // namespace foptim
