#include "HttpServer.h"
#include "../log/log.h"
#include "../muduo/include/Buffer.h"
#include "../muduo/include/TcpConnection.h"
#include "../muduo/include/Timestamp.h"
#include "HttpContext.h"
#include <sstream>
#include <unistd.h>

std::string HttpResponse::toBuffer() const {
  std::ostringstream oss;
  oss << "HTTP/1.1 " << statusCode_ << " " << statusMessage_ << "\r\n";

  if (closeConnection_) {
    oss << "Connection: close\r\n";
  } else {
    size_t contentLength = hasFile() ? fileSize_ : body_.size();
    oss << "Content-Length: " << contentLength << "\r\n";
    oss << "Connection: Keep-Alive\r\n";
  }

  for (const auto &header : headers_) {
    oss << header.first << ": " << header.second << "\r\n";
  }

  oss << "\r\n";

  if (!hasFile()) {
    oss << body_;
  }

  return oss.str();
}

void HttpResponse::toBuffer(Buffer *buf) const {
  buf->append("HTTP/1.1 ", 9);
  buf->append(std::to_string(statusCode_));
  buf->append(" ", 1);
  buf->append(statusMessage_);
  buf->append("\r\n", 2);

  if (closeConnection_) {
    buf->append("Connection: close\r\n", 19);
  } else {
    size_t contentLength = hasFile() ? fileSize_ : body_.size();
    buf->append("Content-Length: ", 16);
    buf->append(std::to_string(contentLength));
    buf->append("\r\n", 2);
    buf->append("Connection: Keep-Alive\r\n", 24);
  }

  for (const auto &header : headers_) {
    buf->append(header.first);
    buf->append(": ", 2);
    buf->append(header.second);
    buf->append("\r\n", 2);
  }
  buf->append("\r\n", 2);

  if (!hasFile()) {
    buf->append(body_);
  }
}

HttpServer::HttpServer(EventLoop *loop, const InetAddress &listenAddr,
                       const std::string &name)
    : server_(loop, listenAddr, name) {
  server_.setConnectionCallback(
      std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
  server_.setMessageCallback(
      std::bind(&HttpServer::onMessage, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));
}

HttpServer::~HttpServer() = default;

void HttpServer::start() { server_.start(); }

void HttpServer::onConnection(const TcpConnectionPtr &conn) {
  if (conn->connected()) {
    // 新连接建立，创建 HttpContext
    contexts_[conn] = std::make_unique<HttpContext>();
  } else {
    // 连接关闭，清理 HttpContext
    contexts_.erase(conn);
  }
}

void HttpServer::onMessage(const TcpConnectionPtr &conn, Buffer *buf,
                           Timestamp receiveTime) {
  auto it = contexts_.find(conn);
  if (it == contexts_.end()) {
    LOG_ERROR("HttpServer::onMessage - no context found for connection");
    conn->send("HTTP/1.1 500 Internal Server Error\r\n\r\n");
    conn->shutdown();
    return;
  }

  HttpContext *context = it->second.get();

  while (buf->readableBytes() > 0) {
    if (!context->parseRequest(buf->peek(), buf->readableBytes())) {
      conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
      conn->shutdown();
      break;
    }

    if (context->gotAll()) {
      buf->retrieve(context->getParsedBytes());

      bool shouldClose = (context->getHeader("connection") == "close") ||
                         (context->version() == "HTTP/1.0" &&
                          context->getHeader("connection") != "keep-alive");

      HttpResponse response(shouldClose);

      if (httpCallback_) {
        httpCallback_(*context, &response);
      } else {
        response.setStatusCode(HttpResponse::k404NotFound);
        response.setStatusMessage("Not Found");
        response.setCloseConnection(true);
      }

      // 发送响应头
      std::string responseStr = response.toBuffer();
      conn->send(responseStr);

      // 如果有文件，使用 sendfile 零拷贝发送
      if (response.hasFile()) {
        conn->sendFile(response.fileFd(), 0, response.fileSize());
      }

      if (response.closeConnection()) {
        conn->shutdown();
        context->reset();
        break;
      }

      // 重置上下文，支持 Pipeline
      context->reset();
    } else {
      // 数据未收全，等待更多数据
      break;
    }
  }
}