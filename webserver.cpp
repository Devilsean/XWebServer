#include "webserver.h"
#include "./log/log.h"
#include <arpa/inet.h>
#include <cassert> // assert
#include <cstring> // strlen, strcpy 等
#include <iostream>
#include <limits.h> // PATH_MAX
#include <netinet/in.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

WebServer::WebServer() {
  m_utils = std::make_unique<Utils>();
  // 1. 使用 C++ 风格获取路径：告别 malloc/free
  char server_path[PATH_MAX];
  if (getcwd(server_path, sizeof(server_path)) != nullptr) {
    m_root = std::string(server_path) + "/root";
  }

  // 2. 这里的 users 数组暂时保持，但改用统一初始化
  users = new http_conn[MAX_FD];
  users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
  // 资源清理逻辑：保持原有 fd 关闭，但注意内存回收
  close(m_epollfd);
  close(m_listenfd);
  close(m_pipefd[1]);
  close(m_pipefd[0]);
  delete[] users;
  delete[] users_timer;
  delete m_pool;
}

void WebServer::init(int port, std::string user, std::string passWord,
                     std::string databaseName, int log_write, int opt_linger,
                     int trigmode, int sql_num, int thread_num, int close_log,
                     int actor_model) {
  m_port = port;
  m_user = std::move(user);
  m_passWord = std::move(passWord);
  m_databaseName = std::move(databaseName);
  m_sql_num = sql_num;
  m_thread_num = thread_num;
  m_log_write = log_write;
  m_OPT_LINGER = opt_linger;
  m_TRIGMode = trigmode;
  m_close_log = close_log;
  m_actormodel = actor_model;
}

// 逻辑简化：根据 trigmode 设置监听和连接模式
void WebServer::trig_mode() {
  m_LISTENTrigMode = (m_TRIGMode >> 1) & 1; // 高位决定监听 fd
  m_CONNTrigMode = m_TRIGMode & 1;          // 低位决定连接 fd
}

void WebServer::log_write() {
  if (m_close_log == 0) {
    int split_num = (m_log_write == 1) ? 800000 : 0;
    Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000,
                              split_num);
  }
}

void WebServer::sql_pool() {
  m_connPool = connection_pool::GetInstance();
  m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306,
                   m_sql_num, m_close_log);
  users->initmysql_result(m_connPool);
}

void WebServer::thread_pool() {
  m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen() {
  m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(m_listenfd >= 0);

  // 优雅关闭
  struct linger tmp = {(m_OPT_LINGER == 1), 1};
  setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(m_port);

  int flag = 1;
  setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
  assert(bind(m_listenfd, (struct sockaddr *)&address, sizeof(address)) >= 0);
  assert(listen(m_listenfd, 5) >= 0);

  m_utils->init(TIMESLOT);

  m_epollfd = epoll_create(5);
  assert(m_epollfd != -1);
  m_utils->addfd(m_epollfd, m_listenfd, false, m_LISTENTrigMode);
  http_conn::m_epollfd = m_epollfd;

  assert(socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd) != -1);
  m_utils->setnonblocking(m_pipefd[1]);
  m_utils->addfd(m_epollfd, m_pipefd[0], false, 0);

  m_utils->addsig(SIGPIPE, SIG_IGN);
  m_utils->addsig(SIGALRM, Utils::sig_handler, false);
  m_utils->addsig(SIGTERM, Utils::sig_handler, false);

  alarm(TIMESLOT);
  Utils::u_pipefd = m_pipefd;
  Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address) {
  users[connfd].init(connfd, client_address, const_cast<char *>(m_root.c_str()),
                     m_CONNTrigMode, m_close_log, m_user, m_passWord,
                     m_databaseName);

  users_timer[connfd].address = client_address;
  users_timer[connfd].sockfd = connfd;

  util_timer *timer = new util_timer;
  timer->user_data = &users_timer[connfd];
  timer->cb_func = cb_func;
  timer->expire = time(nullptr) + 3 * TIMESLOT;

  users_timer[connfd].timer = timer;
  m_utils->m_timer_lst.add_timer(timer);
}

void WebServer::adjust_timer(util_timer *timer) {
  timer->expire = time(nullptr) + 3 * TIMESLOT;
  m_utils->m_timer_lst.adjust_timer(timer);
  LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd) {
  timer->cb_func(&users_timer[sockfd]);
  if (timer)
    m_utils->m_timer_lst.del_timer(timer);
  LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclientdata() {
  struct sockaddr_in client_address;
  socklen_t client_addrlength = sizeof(client_address);

  auto handle_accept = [&]() {
    int connfd = accept(m_listenfd, (struct sockaddr *)&client_address,
                        &client_addrlength);
    if (connfd < 0)
      return false;
    if (http_conn::m_user_count >= MAX_FD) {
      m_utils->show_error(connfd, "Internal server busy");
      return false;
    }
    timer(connfd, client_address);
    return true;
  };

  if (m_LISTENTrigMode == 0)
    return handle_accept();
  else {
    while (handle_accept())
      ;
    return false;
  }
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server) {
  char signals[1024];
  int ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
  if (ret <= 0)
    return false;

  for (int i = 0; i < ret; ++i) {
    if (signals[i] == SIGALRM)
      timeout = true;
    else if (signals[i] == SIGTERM)
      stop_server = true;
  }
  return true;
}

// 优化：移除 CPU 轮询
void WebServer::dealwithread(int sockfd) {
  util_timer *timer = users_timer[sockfd].timer;
  if (m_actormodel == 1) { // Reactor
    if (timer)
      adjust_timer(timer);
    m_pool->append(users + sockfd, 0);
    // 注意：原代码的 while(true) 轮询 improv 在多线程环境下非常低效
    // 建议在线程池任务完成后通过 epoll 发送事件，而不是在此同步等待
  } else { // Proactor
    if (users[sockfd].read_once()) {
      m_pool->append_p(users + sockfd);
      if (timer)
        adjust_timer(timer);
    } else
      deal_timer(timer, sockfd);
  }
}

void WebServer::dealwithwrite(int sockfd) {
  util_timer *timer = users_timer[sockfd].timer;
  if (m_actormodel == 1) { // Reactor
    if (timer)
      adjust_timer(timer);
    m_pool->append(users + sockfd, 1);
  } else { // Proactor
    if (users[sockfd].write()) {
      if (timer)
        adjust_timer(timer);
    } else
      deal_timer(timer, sockfd);
  }
}

void WebServer::eventLoop() {
  bool timeout = false;
  bool stop_server = false;

  while (!stop_server) {
    int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
    if (number < 0 && errno != EINTR)
      break;

    for (int i = 0; i < number; i++) {
      int sockfd = events[i].data.fd;
      if (sockfd == m_listenfd) {
        dealclientdata();
      } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        deal_timer(users_timer[sockfd].timer, sockfd);
      } else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
        dealwithsignal(timeout, stop_server);
      } else if (events[i].events & EPOLLIN) {
        dealwithread(sockfd);
      } else if (events[i].events & EPOLLOUT) {
        dealwithwrite(sockfd);
      }
    }
    if (timeout) {
      m_utils->timer_handler();
      timeout = false;
    }
  }
}