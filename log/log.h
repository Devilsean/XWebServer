#ifndef LOG_H
#define LOG_H

#include "../lock/locker.h"
#include "block_queue.h"
#include <iostream>
#include <memory> // 智能指针
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <thread> // C++11 线程

using namespace std;

class Log {
public:
  // C++11 懒汉模式，静态局部变量保证线程安全
  static Log *get_instance() {
    static Log instance;
    return &instance;
  }

  // 禁用拷贝和赋值
  Log(const Log &) = delete;
  Log &operator=(const Log &) = delete;

  // 异步日志写线程函数（C++11 风格）
  static void flush_log_thread() { Log::get_instance()->async_write_log(); }

  bool init(const char *file_name, int close_log, int log_buf_size = 8192,
            int split_lines = 5000000, int max_queue_size = 0);

  void write_log(int level, const char *format, ...);
  void flush(void);

private:
  Log();
  virtual ~Log();

  // 异步写逻辑
  void async_write_log() {
    string single_log;
    // 这里的 m_log_queue 需要是一个支持 C++11 的阻塞队列
    while (m_log_queue->pop(single_log)) {
      // 使用之前改好的 locker
      std::lock_guard<std::mutex> lock(m_mutex.get());
      fputs(single_log.c_str(), m_fp);
    }
  }

private:
  char dir_name[128];
  char log_name[128];
  int m_split_lines;
  int m_log_buf_size;
  long long m_count;
  int m_today;
  FILE *m_fp;
  char *m_buf;
  // 使用智能指针管理队列
  std::unique_ptr<block_queue<string>> m_log_queue;
  bool m_is_async;
  locker m_mutex;
  int m_close_log;
};

#define LOG_DEBUG(format, ...)                                                 \
  do {                                                                         \
    if (0 == m_close_log) {                                                    \
      Log::get_instance()->write_log(0, format, ##__VA_ARGS__);                \
      Log::get_instance()->flush();                                            \
    }                                                                          \
  } while (0)

#define LOG_INFO(format, ...)                                                  \
  do {                                                                         \
    if (0 == m_close_log) {                                                    \
      Log::get_instance()->write_log(1, format, ##__VA_ARGS__);                \
      Log::get_instance()->flush();                                            \
    }                                                                          \
  } while (0)

#define LOG_WARN(format, ...)                                                  \
  do {                                                                         \
    if (0 == m_close_log) {                                                    \
      Log::get_instance()->write_log(2, format, ##__VA_ARGS__);                \
      Log::get_instance()->flush();                                            \
    }                                                                          \
  } while (0)

#define LOG_ERROR(format, ...)                                                 \
  do {                                                                         \
    if (0 == m_close_log) {                                                    \
      Log::get_instance()->write_log(3, format, ##__VA_ARGS__);                \
      Log::get_instance()->flush();                                            \
    }                                                                          \
  } while (0)

#endif
