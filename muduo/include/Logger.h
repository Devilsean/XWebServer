#pragma once

#include "noncopyable.h"
#include <stdio.h>
#include <string>

// LOG_INFO("%s %d", arg1, arg2)
#define MUDUO_LOG_INFO(logmsgFormat, ...)                                      \
  do {                                                                         \
    Logger &logger = Logger::instance();                                       \
    logger.setLogLevel(INFO);                                                  \
    char buf[1024] = {0};                                                      \
    snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);                          \
    logger.log(buf);                                                           \
  } while (0)

#define MUDUO_LOG_ERROR(logmsgFormat, ...)                                     \
  do {                                                                         \
    Logger &logger = Logger::instance();                                       \
    logger.setLogLevel(ERROR);                                                 \
    char buf[1024] = {0};                                                      \
    snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);                          \
    logger.log(buf);                                                           \
  } while (0)

#define MUDUO_LOG_FATAL(logmsgFormat, ...)                                     \
  do {                                                                         \
    Logger &logger = Logger::instance();                                       \
    logger.setLogLevel(FATAL);                                                 \
    char buf[1024] = {0};                                                      \
    snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);                          \
    logger.log(buf);                                                           \
    exit(-1);                                                                  \
  } while (0)

#ifdef MUDEBUG
#define MUDUO_LOG_DEBUG(logmsgFormat, ...)                                     \
  do {                                                                         \
    Logger &logger = Logger::instance();                                       \
    logger.setLogLevel(DEBUG);                                                 \
    char buf[1024] = {0};                                                      \
    snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);                          \
    logger.log(buf);                                                           \
  } while (0)
#else
#define MUDUO_LOG_DEBUG(logmsgFormat, ...)
#endif

// 定义日志的级别 INFO ERROR FATAL DEBUG
enum LogLevel {
  INFO,  // 普通信息
  ERROR, // 错误信息
  FATAL, // core dump信息
  DEBUG, // 调试信息
};

// 输出一个日志类

class Logger : noncopyable {
public:
  // 获取日志唯一的实例对象 单例
  static Logger &instance();
  // 设置日志级别
  void setLogLevel(int level);
  // 写日志
  void log(std::string msg);
  // 初始化日志文件输出
  void initFile(const char *filename);
  // 设置是否关闭日志输出
  void setCloseLog(int closeLog);

private:
  Logger();
  ~Logger();
  int logLevel_;
  FILE *fp_;
  int closeLog_;
};