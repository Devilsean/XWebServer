#ifndef CONFIG_H
#define CONFIG_H

#include <string>

// 前置声明，不需要包含完整的 webserver.h，减少编译依赖
class WebServer;

class Config {
public:
  Config();
  ~Config() = default; // 使用 C++11 default 析构

  // 解析命令行参数
  void parse_arg(int argc, char *argv[]);

  // 基础配置参数 (C++11 类内初始化，确保默认安全) ---

  int PORT{9006};        // 端口号，默认9006
  int LOGWrite{0};       // 日志写入方式，默认同步
  int TRIGMode{0};       // 触发组合模式，默认 listenfd LT + connfd LT
  int LISTENTrigmode{0}; // listenfd触发模式
  int CONNTrigmode{0};   // connfd触发模式
  int OPT_LINGER{0};     // 优雅关闭链接
  int sql_num{8};        // 数据库连接池数量
  int thread_num{8};     // 线程池内的线程数量
  int close_log{0};      // 是否关闭日志
  int actor_model{0};    // 并发模型选择 (0: Proactor, 1: Reactor)
};

#endif