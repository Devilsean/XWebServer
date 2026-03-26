#include <string.h>
#include <time.h>

#include "../include/Logger.h"
#include "../include/Timestamp.h"

// 获取日志唯一的实例对象 单例
Logger &Logger::instance() {
  static Logger logger;
  return logger;
}

Logger::Logger() : logLevel_(INFO), fp_(nullptr), closeLog_(0) {}

Logger::~Logger() {
  if (fp_ != nullptr && fp_ != stdout) {
    fclose(fp_);
  }
}

// 设置日志级别
void Logger::setLogLevel(int level) { logLevel_ = level; }

// 设置是否关闭日志
void Logger::setCloseLog(int closeLog) { closeLog_ = closeLog; }

// 初始化日志文件输出
void Logger::initFile(const char *filename) {
  if (fp_ != nullptr && fp_ != stdout) {
    fclose(fp_);
  }

  if (filename == nullptr || strlen(filename) == 0) {
    fp_ = stdout; // 默认输出到终端
    return;
  }

  // 按日期生成文件名
  time_t t = time(nullptr);
  struct tm *sys_tm = localtime(&t);
  struct tm my_tm = *sys_tm;

  char log_full_name[512] = {0};
  const char *p = strrchr(filename, '/');

  if (p == nullptr) {
    snprintf(log_full_name, 511, "%d_%02d_%02d_%s", my_tm.tm_year + 1900,
             my_tm.tm_mon + 1, my_tm.tm_mday, filename);
  } else {
    char dir_name[128] = {0};
    char log_name[128] = {0};
    strcpy(log_name, p + 1);
    strncpy(dir_name, filename, p - filename + 1);
    snprintf(log_full_name, 511, "%s%d_%02d_%02d_%s", dir_name,
             my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
  }

  fp_ = fopen(log_full_name, "a");
  if (fp_ == nullptr) {
    fp_ = stdout; // 失败则回退到终端
  }
}

// 写日志 [级别信息] time : msg
void Logger::log(std::string msg) {
  if (closeLog_ == 1)
    return;

  std::string pre = "";
  switch (logLevel_) {
  case INFO:
    pre = "[INFO]";
    break;
  case ERROR:
    pre = "[ERROR]";
    break;
  case FATAL:
    pre = "[FATAL]";
    break;
  case DEBUG:
    pre = "[DEBUG]";
    break;
  default:
    break;
  }

  FILE *output = (fp_ != nullptr) ? fp_ : stdout;
  fprintf(output, "%s%s : %s\n", pre.c_str(),
          Timestamp::now().toString().c_str(), msg.c_str());
  fflush(output);
}