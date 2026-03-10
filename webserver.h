#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <vector>

#include "./CGImysql/sql_connection_pool.h"
#include "./http/http_conn.h"
#include "./threadpool/threadpool.h"
#include "./timer/lst_timer.h"

class WebServer {
public:
  // C++11 constexpr：类内常量定义，更安全
  static constexpr int MAX_FD = 65536;           // 最大文件描述符
  static constexpr int MAX_EVENT_NUMBER = 10000; // 最大事件数
  static constexpr int TIMESLOT = 5;             // 最小超时单位

public:
  WebServer();
  ~WebServer();

  // 显式禁用拷贝和赋值 (C++11)
  WebServer(const WebServer &) = delete;
  WebServer &operator=(const WebServer &) = delete;

  void init(int port, std::string user, std::string passWord,
            std::string databaseName, int log_write, int opt_linger,
            int trigmode, int sql_num, int thread_num, int close_log,
            int actor_model);

  // 逻辑初始化与运行接口
  void thread_pool();
  void sql_pool();
  void log_write();
  void trig_mode();
  void eventListen();
  void eventLoop();

  // 定时器与连接管理
  void timer(int connfd, struct sockaddr_in client_address);
  void adjust_timer(util_timer *timer);
  void deal_timer(util_timer *timer, int sockfd);

  // 事件处理核心
  bool dealclientdata();
  bool dealwithsignal(bool &timeout, bool &stop_server);
  void dealwithread(int sockfd);
  void dealwithwrite(int sockfd);

public:
  // 基础配置
  int m_port = 0;
  std::string m_root; // 改用 std::string，告别 char*
  int m_log_write = 0;
  int m_close_log = 0;
  int m_actormodel = 0;

  int m_pipefd[2]{}; // 使用 {} 初始化数组
  int m_epollfd = -1;

  // std::unique_ptr<http_conn[]>，或者保持原始指针但严格管理生命周期
  http_conn *users = nullptr;

  // 数据库相关
  connection_pool *m_connPool = nullptr;
  std::string m_user;
  std::string m_passWord;
  std::string m_databaseName;
  int m_sql_num = 0;

  // 线程池相关（使用泛型）
  threadpool<http_conn> *m_pool = nullptr;
  int m_thread_num = 0;

  // epoll 事件数组
  epoll_event events[MAX_EVENT_NUMBER]{};

  int m_listenfd = -1;
  int m_OPT_LINGER = 0;
  int m_TRIGMode = 0;
  int m_LISTENTrigMode = 0;
  int m_CONNTrigMode = 0;

  // 定时器相关
  client_data *users_timer = nullptr;
  // Utils 最好也使用 unique_ptr 或者确保包含其定义
  std::unique_ptr<Utils> m_utils;
};

#endif