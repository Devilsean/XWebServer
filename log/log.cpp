#include "log.h"
#include <string.h>
#include <sys/time.h>
#include <thread>
#include <time.h>

using namespace std;

Log::Log() {
  m_count = 0;
  m_is_async = false;
  m_fp = nullptr;
}

Log::~Log() {
  if (m_fp != nullptr) {
    fclose(m_fp);
  }
}

bool Log::init(const char *file_name, int close_log, int log_buf_size,
               int split_lines, int max_queue_size) {
  // 异步设置
  if (max_queue_size >= 1) {
    m_is_async = true;
    // 用智能指针的 reset，不需要手动 delete 了
    m_log_queue.reset(new block_queue<string>(max_queue_size));

    // C++11 开启线程，直接传静态成员函数即可，不需要传 NULL 指针了
    std::thread tid(flush_log_thread);
    tid.detach(); // 这里的效果和 pthread_detach 一样
  }

  m_close_log = close_log;
  m_log_buf_size = log_buf_size;
  m_buf = new char[m_log_buf_size];
  memset(m_buf, '\0', m_log_buf_size);
  m_split_lines = split_lines;

  time_t t = time(nullptr);
  struct tm *sys_tm = localtime(&t);
  struct tm my_tm = *sys_tm;

  const char *p = strrchr(file_name, '/');
  char log_full_name[512] = {0};

  if (p == nullptr) {
    snprintf(log_full_name, 511, "%d_%02d_%02d_%s", my_tm.tm_year + 1900,
             my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
  } else {
    strcpy(log_name, p + 1);
    strncpy(dir_name, file_name, p - file_name + 1);
    snprintf(log_full_name, 511, "%s%d_%02d_%02d_%s", dir_name,
             my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
  }

  m_today = my_tm.tm_mday;
  m_fp = fopen(log_full_name, "a");
  return m_fp != nullptr;
}

void Log::write_log(int level, const char *format, ...) {
  struct timeval now = {0, 0};
  gettimeofday(&now, nullptr);
  time_t t = now.tv_sec;
  struct tm *sys_tm = localtime(&t);
  struct tm my_tm = *sys_tm;

  // level 判断逻辑不变
  char s[16] = {0};
  switch (level) {
  case 0:
    strcpy(s, "[debug]:");
    break;
  case 1:
    strcpy(s, "[info]:");
    break;
  case 2:
    strcpy(s, "[warn]:");
    break;
  case 3:
    strcpy(s, "[erro]:");
    break;
  default:
    strcpy(s, "[info]:");
    break;
  }

  // 使用 lock_guard
  {
    // 这一对大括号是为了控制 lock_guard 的作用域
    std::lock_guard<std::mutex> lock(m_mutex.get());
    m_count++;

    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
      char new_log[512] = {0};
      fflush(m_fp);
      fclose(m_fp);
      char tail[16] = {0};
      snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900,
               my_tm.tm_mon + 1, my_tm.tm_mday);

      if (m_today != my_tm.tm_mday) {
        snprintf(new_log, 511, "%s%s%s", dir_name, tail, log_name);
        m_today = my_tm.tm_mday;
        m_count = 0;
      } else {
        snprintf(new_log, 511, "%s%s%s.%lld", dir_name, tail, log_name,
                 m_count / m_split_lines);
      }
      m_fp = fopen(new_log, "a");
    }
  } // 这里自动解锁

  va_list valst;
  va_start(valst, format);
  string log_str;

  {
    std::lock_guard<std::mutex> lock(m_mutex.get());
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;
  }

  if (m_is_async && !m_log_queue->full()) {
    m_log_queue->push(log_str);
  } else {
    std::lock_guard<std::mutex> lock(m_mutex.get());
    fputs(log_str.c_str(), m_fp);
  }
  va_end(valst);
}

void Log::flush(void) {
  std::lock_guard<std::mutex> lock(m_mutex.get());
  fflush(m_fp);
}