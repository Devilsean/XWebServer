#ifndef LOCKER_H
#define LOCKER_H

#include <condition_variable>
#include <exception>
#include <mutex>
#include <semaphore.h> // C++11没有内置信号量，保留

// 信号量类：保持原样或微调
class sem {
public:
  sem(int num = 0) {
    if (sem_init(&m_sem, 0, num) != 0) {
      throw std::exception();
    }
  }
  ~sem() { sem_destroy(&m_sem); }
  bool wait() { return sem_wait(&m_sem) == 0; }
  bool post() { return sem_post(&m_sem) == 0; }

private:
  sem_t m_sem;
};

// 互斥锁类：使用 std::mutex 封装
class locker {
public:
  locker() = default; // 自动调用 std::mutex 的构造函数
  ~locker() = default;

  bool lock() {
    m_mutex.lock();
    return true;
  }
  bool unlock() {
    m_mutex.unlock();
    return true;
  }

  // 返回原生引用，方便配合 std::unique_lock 使用
  std::mutex &get() { return m_mutex; }

private:
  std::mutex m_mutex;
};

// 条件变量类：使用 std::condition_variable 封装
class cond {
public:
  cond() = default;
  ~cond() = default;

  bool wait(std::unique_lock<std::mutex> &lock) {
    m_cond.wait(lock);
    return true;
  }

  bool timewait(std::mutex *m_mutex, struct timespec t) {
    std::unique_lock<std::mutex> lock(*m_mutex, std::adopt_lock);
    // 将 timespec 转换为 C++11 的 duration
    auto tp = std::chrono::system_clock::from_time_t(t.tv_sec) +
              std::chrono::nanoseconds(t.tv_nsec);
    std::cv_status status = m_cond.wait_until(lock, tp);
    lock.release();
    return status == std::cv_status::no_timeout;
  }

  bool signal() {
    m_cond.notify_one();
    return true;
  }
  bool broadcast() {
    m_cond.notify_all();
    return true;
  }

private:
  std::condition_variable m_cond;
};

#endif