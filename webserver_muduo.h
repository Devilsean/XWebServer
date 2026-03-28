#ifndef WEBSERVER_MUDUO_H
#define WEBSERVER_MUDUO_H

#include "./CGImysql/sql_connection_pool.h"
#include "./http/HttpServer.h"
#include "./muduo/include/EventLoop.h"
#include "./muduo/include/InetAddress.h"
#include "muduo/include/noncopyable.h"
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Muduo 风格的 WebServer
class WebServerMuduo:noncopyable {
public:
  WebServerMuduo(EventLoop *loop, const InetAddress &listenAddr,
                 const std::string &name);
  ~WebServerMuduo();

  // 初始化配置
  void init(std::string user, std::string passWord, std::string databaseName,
            int log_write, int close_log, int sql_num, int thread_num);

  // 设置线程数
  void setThreadNum(int numThreads);

  // 启动服务器
  void start();

private:
  // HTTP 请求处理回调
  void onRequest(const HttpContext &req, HttpResponse *resp);

  // 业务逻辑处理
  void handleStaticFile(const HttpContext &req, HttpResponse *resp);
  void handleCGI(const HttpContext &req, HttpResponse *resp);

  // 初始化数据库连接池
  void initSqlPool();

  // 加载用户数据
  void loadUsers();

  EventLoop *loop_;
  std::unique_ptr<HttpServer> server_;

  // 数据库相关
  connection_pool *connPool_;
  std::string sqlUser_;
  std::string sqlPasswd_;
  std::string sqlName_;
  int sqlNum_;

  // 日志配置
  int logWrite_;
  int closeLog_;

  // 线程数
  int threadNum_;

  // 文档根目录
  std::string docRoot_;

  // 用户数据缓存
  std::map<std::string, std::string> users_;
  std::mutex usersMutex_;
  std::atomic<bool> usersLoaded_{false};
  std::condition_variable usersLoadedCV_;
  std::thread loadUsersThread_;
};

#endif