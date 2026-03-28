#include "./log/log.h"
#include "./muduo/include/EventLoop.h"
#include "./muduo/include/InetAddress.h"
#include "config.h"
#include "webserver_muduo.h"
#include <unistd.h>
#include <string>

int main(int argc, char *argv[]) {
  // 1. 数据库配置（建议后期放入 config 或环境变量）
  std::string user = "webuser";
  std::string passwd = "Web123456!";
  std::string databasename = "tinyweb";

  // 2. 命令行解析：获取用户自定义运行参数
  Config config;
  config.parse_arg(argc, argv);

  // 3. 初始化日志系统（原项目文件日志）
  if (config.close_log == 0) {
    int split_num = (config.LOGWrite == 1) ? 800000 : 0;
    Log::get_instance()->init("./ServerLog", config.close_log, 2000, 800000,
                              split_num);
    LOG_INFO("=== Muduo WebServer Starting ===");
  }

  // 4. 创建 EventLoop（One Loop Per Thread 核心）
  EventLoop loop;

  // 5. 配置监听地址
  InetAddress listenAddr(config.PORT);

  // 6. 实例化 Muduo 风格 WebServer
  WebServerMuduo server(&loop, listenAddr, "TinyWebServer-Muduo");

  // 7. 初始化服务器配置
  server.init(user, passwd, databasename, config.LOGWrite, config.close_log,
              config.sql_num, config.thread_num);

  // 8. 设置工作线程数（IO线程池）
  server.setThreadNum(config.thread_num);

  // 9. 启动服务器
  LOG_INFO("Server starting on port %d", config.PORT);
  server.start();

  // 10. 启动事件循环（阻塞在此，等待连接）
  LOG_INFO("EventLoop started");
  loop.loop();

  return 0;
}