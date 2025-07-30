#pragma once
#include <shared_mutex>
#include <tracy/Tracy.hpp>

namespace foptim {

template <class T>
class Mutex;

template <class T>
class MutMutexGuard {
  Mutex<T> *const mutex;

 public:
  MutMutexGuard(Mutex<T> *cont) : mutex(cont) { mutex->_mutex.lock(); }
  ~MutMutexGuard() { mutex->_mutex.unlock(); }
  T *operator->() { return &mutex->_contained; }
  T &operator*() { return mutex->_contained; }
};

template <class T>
class ConstMutexGuard {
  Mutex<T> *const mutex;

 public:
  ConstMutexGuard(Mutex<T> *cont) : mutex(cont) { mutex->_mutex.lock_shared(); }
  ~ConstMutexGuard() { mutex->_mutex.unlock_shared(); }
  const T *operator->() { return &mutex->_contained; }
  const T &operator*() { return mutex->_contained; }
};

template <class T>
class Mutex {
 public:
  T _contained;
#ifdef TRACY_ENABLE
  tracy::SharedLockable<std::shared_mutex> _mutex = {
      []() -> const tracy::SourceLocationData * {
        static tracy::SourceLocationData srcloc{nullptr, typeid(T).name(),
                                                __FILE__, __LINE__, 0};
        return &srcloc;
      }()};
#else
  std::shared_mutex _mutex;
#endif
  [[nodiscard]] ConstMutexGuard<T> scoped_lock() const {
    return ConstMutexGuard<T>{const_cast<Mutex<T> *>(this)};
  }

  [[nodiscard]] MutMutexGuard<T> scoped_lock() {
    return MutMutexGuard<T>{this};
  }
};

}  // namespace foptim
