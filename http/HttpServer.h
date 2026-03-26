#pragma once

#include "../muduo/include/TcpServer.h"
#include "HttpContext.h"
#include <map>
#include <memory>
#include <string>

class HttpRequest;
class HttpResponse;

// HTTP 响应类
class HttpResponse {
public:
  enum HttpStatusCode {
    k200Ok = 200,
    k400BadRequest = 400,
    k403Forbidden = 403,
    k404NotFound = 404,
    k500InternalError = 500,
    k503ServiceUnavailable = 503,
  };

  HttpResponse(bool close)
      : statusCode_(k200Ok), closeConnection_(close), fileFd_(-1),
        fileSize_(0) {}

  void setStatusCode(HttpStatusCode code) { statusCode_ = code; }
  void setStatusMessage(const std::string &message) {
    statusMessage_ = message;
  }
  void setCloseConnection(bool on) { closeConnection_ = on; }
  void setContentType(const std::string &contentType) {
    addHeader("Content-Type", contentType);
  }
  void addHeader(const std::string &key, const std::string &value) {
    headers_[key] = value;
  }
  void setBody(const std::string &body) { body_ = body; }
  const std::string &body() const { return body_; }
  void setFile(int fd, size_t size) {
    fileFd_ = fd;
    fileSize_ = size;
  }

  bool closeConnection() const { return closeConnection_; }
  bool hasFile() const { return fileFd_ >= 0; }
  int fileFd() const { return fileFd_; }
  size_t fileSize() const { return fileSize_; }

  std::string toBuffer() const;
  void toBuffer(Buffer *buf) const;

private:
  std::map<std::string, std::string> headers_;
  HttpStatusCode statusCode_;
  std::string statusMessage_;
  bool closeConnection_;
  std::string body_;
  int fileFd_;
  size_t fileSize_;
};

// HTTP 服务器
class HttpServer {
public:
  using HttpCallback = std::function<void(const HttpContext &, HttpResponse *)>;

  HttpServer(EventLoop *loop, const InetAddress &listenAddr,
             const std::string &name);
  ~HttpServer();

  void setHttpCallback(const HttpCallback &cb) { httpCallback_ = cb; }

  void setThreadNum(int numThreads) { server_.setThreadNum(numThreads); }

  void start();

private:
  void onConnection(const TcpConnectionPtr &conn);
  void onMessage(const TcpConnectionPtr &conn, Buffer *buf,
                 Timestamp receiveTime);

  TcpServer server_;
  HttpCallback httpCallback_;
  // 管理每个连接的 HttpContext
  std::map<TcpConnectionPtr, std::unique_ptr<HttpContext>> contexts_;
};