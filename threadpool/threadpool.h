#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "../CGImysql/sql_connection_pool.h"
#include "../lock/locker.h"
#include <cstdio>
#include <exception>
#include <list>
#include <thread>
#include <vector> // 用 vector 存线程更现代

template <typename T> class threadpool {
public:
  threadpool(int actor_model, connection_pool *connPool, int thread_number = 8,
             int max_request = 10000);
  ~threadpool() = default; // 线程自动管理，不需要手动 delete 了

  bool append(T *request, int state);
  bool append_p(T *request);

private:
  // worker 删了，直接用 Lambda，但为了结构清晰先留个私有的 run
  void run();

private:
  int m_thread_number;
  int m_max_requests;
  int m_actor_model;
  std::vector<std::thread> m_threads; // 线程数组变容器
  std::list<T *> m_workqueue;
  locker m_queuelocker;
  sem m_queuestat;
  connection_pool *m_connPool;
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool,
                          int thread_number, int max_requests)
    : m_thread_number(thread_number), m_actor_model(actor_model),
      m_max_requests(max_requests), m_connPool(connPool) {

  if (thread_number <= 0 || max_requests <= 0)
    throw std::exception();

  // 直接在构造函数里用 std::thread 初始化
  for (int i = 0; i < thread_number; ++i) {
    // 使用 Lambda 表达式直接绑定成员函数 run
    // 这样就不用写那个又丑又长的 worker 回调和 (void*) 转换了
    m_threads.emplace_back([this] { this->run(); });

    // C++11 线程对象必须 detach 或 join，这里我们 detach
    m_threads.back().detach();
  }
}

template <typename T> bool threadpool<T>::append(T *request, int state) {
  {
    std::lock_guard<std::mutex> lock(m_queuelocker.get()); // RAII 加锁
    if (m_workqueue.size() >= (size_t)m_max_requests) {
      return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
  } // 自动解锁
  m_queuestat.post();
  return true;
}

template <typename T> bool threadpool<T>::append_p(T *request) {
  {
    std::lock_guard<std::mutex> lock(m_queuelocker.get());
    if (m_workqueue.size() >= (size_t)m_max_requests) {
      return false;
    }
    m_workqueue.push_back(request);
  }
  m_queuestat.post();
  return true;
}

template <typename T> void threadpool<T>::run() {
  while (true) {
    m_queuestat.wait(); // 信号量等待

    T *request = nullptr;
    {
      std::lock_guard<std::mutex> lock(m_queuelocker.get());
      if (m_workqueue.empty()) {
        continue;
      }
      request = m_workqueue.front();
      m_workqueue.pop_front();
    }

    if (!request)
      continue;

    if (1 == m_actor_model) { // Reactor
      if (0 == request->m_state) {
        if (request->read_once()) {
          request->improv = 1;
          connectionRAII mysqlcon(&request->mysql, m_connPool);
          request->process();
        } else {
          request->improv = 1;
          request->timer_flag = 1;
        }
      } else { // Proactor
        if (request->write()) {
          request->improv = 1;
        } else {
          request->improv = 1;
          request->timer_flag = 1;
        }
      }
    } else {
      connectionRAII mysqlcon(&request->mysql, m_connPool);
      request->process();
    }
  }
}
#endif