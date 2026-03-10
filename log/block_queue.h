#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include "../lock/locker.h"
#include <iostream>
#include <mutex>
#include <stdlib.h>
#include <sys/time.h>

template <class T> class block_queue {
public:
  block_queue(int max_size = 1000) {
    if (max_size <= 0) {
      exit(-1);
    }

    m_max_size = max_size;
    m_array = new T[max_size];
    m_size = 0;
    m_front = -1;
    m_back = -1;
  }

  void clear() {
    std::lock_guard<std::mutex> lock(m_mutex.get()); // RAII 加锁
    m_size = 0;
    m_front = -1;
    m_back = -1;
  }

  ~block_queue() {
    std::lock_guard<std::mutex> lock(m_mutex.get());
    if (m_array != nullptr)
      delete[] m_array;
  }

  bool full() {
    std::lock_guard<std::mutex> lock(m_mutex.get());
    return m_size >= m_max_size;
  }

  bool empty() {
    std::lock_guard<std::mutex> lock(m_mutex.get());
    return 0 == m_size;
  }

  bool front(T &value) {
    std::lock_guard<std::mutex> lock(m_mutex.get());
    if (0 == m_size) {
      return false;
    }
    value = m_array[m_front];
    return true;
  }

  bool back(T &value) {
    std::lock_guard<std::mutex> lock(m_mutex.get());
    if (0 == m_size) {
      return false;
    }
    value = m_array[m_back];
    return true;
  }

  int size() {
    std::lock_guard<std::mutex> lock(m_mutex.get());
    return m_size;
  }

  int max_size() {
    std::lock_guard<std::mutex> lock(m_mutex.get());
    return m_max_size;
  }

  bool push(const T &item) {
    std::lock_guard<std::mutex> lock(m_mutex.get());
    if (m_size >= m_max_size) {
      m_cond.broadcast();
      return false;
    }

    m_back = (m_back + 1) % m_max_size;
    m_array[m_back] = item;
    m_size++;

    m_cond.broadcast();
    return true;
  }

  // pop 必须用 unique_lock，因为 wait 会在阻塞时自动释放锁
  bool pop(T &item) {
    // 由于我们之前的 locker 类 wait 接口设计是传入 mutex 指针
    // 这里直接调用你改好的接口
    m_mutex.lock();
    std::unique_lock<std::mutex> lock(m_mutex.get()); // RAII 加锁
    while (m_size <= 0) {
      if (!m_cond.wait(lock)) {
        m_mutex.unlock();
        return false;
      }
    }

    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size--;
    m_mutex.unlock();
    return true;
  }

  bool pop(T &item, int ms_timeout) {
    struct timespec t = {0, 0};
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);

    m_mutex.lock();
    if (m_size <= 0) {
      t.tv_sec = now.tv_sec + ms_timeout / 1000;
      t.tv_nsec = (now.tv_usec + (ms_timeout % 1000) * 1000) * 1000;

      // 处理纳秒进位，防止 timewait 报错
      if (t.tv_nsec >= 1000000000) {
        t.tv_sec++;
        t.tv_nsec -= 1000000000;
      }

      if (!m_cond.timewait(&m_mutex.get(), t)) {
        m_mutex.unlock();
        return false;
      }
    }

    if (m_size <= 0) {
      m_mutex.unlock();
      return false;
    }

    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size--;
    m_mutex.unlock();
    return true;
  }

private:
  locker m_mutex;
  cond m_cond;

  T *m_array;
  int m_size;
  int m_max_size;
  int m_front;
  int m_back;
};

#endif