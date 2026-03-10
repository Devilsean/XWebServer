#include "config.h"
#include "webserver.h"
#include <string>

int main(int argc, char *argv[]) {
  // 1. 数据库配置（建议后期放入 config 或环境变量）
  // 使用 std::string 配合 C++11 特性
  std::string user = "webuser";
  std::string passwd = "Web123456!";
  std::string databasename = "tinyweb";

  // 2. 命令行解析：获取用户自定义运行参数
  Config config;
  config.parse_arg(argc, argv);

  // 3. 实例化服务器
  WebServer server;

  // 4. 初始化：将所有配置参数送入 WebServer 核心
  // 这里利用了我们在 WebServer::init 中重构后的 std::string 传参方式
  server.init(config.PORT, user, passwd, databasename, config.LOGWrite,
              config.OPT_LINGER, config.TRIGMode, config.sql_num,
              config.thread_num, config.close_log, config.actor_model);

  // 5. 启动各大核心模块（按照依赖顺序）
  // a. 开启日志系统（必须最先启动，方便后续模块报错记录）
  server.log_write();

  // b. 开启数据库连接池（内部会执行 initmysql_result 缓存用户信息）
  server.sql_pool();

  // c. 开启线程池（准备处理并发请求）
  server.thread_pool();

  // d. 配置事件触发模式（LT/ET 组合）
  server.trig_mode();

  // e. 网络监听（创建 socket, bind, listen, epoll_create）
  server.eventListen();

  // 6. 进入主循环：这是程序驻留内存的地方
  // 内部封装了 epoll_wait 和信号处理
  server.eventLoop();

  return 0;
}