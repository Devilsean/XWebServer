#pragma once

#include <map>
#include <string>

// HTTP 请求解析上下文，用于 Muduo 风格的状态机
class HttpContext {
public:
  enum HttpRequestParseState {
    kExpectRequestLine,
    kExpectHeaders,
    kExpectBody,
    kGotAll,
  }; 

  enum Method { kInvalid, kGet, kPost, kHead, kPut, kDelete };

  HttpContext() : state_(kExpectRequestLine) {}

  // 解析请求，返回 true 表示成功解析完整请求
  bool parseRequest(const char *buf, size_t len);

  bool gotAll() const { return state_ == kGotAll; }

  size_t getParsedBytes() const { return parsedBytes_; }

  void reset() {
    state_ = kExpectRequestLine;
    method_ = kInvalid;
    path_.clear();
    query_.clear();
    version_.clear();
    headers_.clear();
    body_.clear();
    parsedBytes_ = 0;
    buffer_.clear();
  }

  Method method() const { return method_; }
  const std::string &path() const { return path_; }
  const std::string &query() const { return query_; }
  const std::string &version() const { return version_; }
  const std::string &getHeader(const std::string &field) const {
    auto it = headers_.find(field);
    return it != headers_.end() ? it->second : emptyString_;
  }
  const std::map<std::string, std::string> &headers() const { return headers_; }
  const std::string &body() const { return body_; }

  void setMethod(Method m) { method_ = m; }
  void setPath(const std::string &p) { path_ = p; }
  void setQuery(const std::string &q) { query_ = q; }
  void setVersion(const std::string &v) { version_ = v; }
  void addHeader(const std::string &key, const std::string &value) {
    headers_[key] = value;
  }
  void setBody(const std::string &b) { body_ = b; }

private:
  bool processRequestLine(const char *begin, const char *end);
  bool processHeaders(const char *begin, const char *end);

  HttpRequestParseState state_;
  Method method_;
  std::string path_;
  std::string query_;
  std::string version_;
  std::map<std::string, std::string> headers_;
  std::string body_;
  size_t parsedBytes_ = 0;
  std::string buffer_; // 用于累积分片数据

  static std::string emptyString_;
};