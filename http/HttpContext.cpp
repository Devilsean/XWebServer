#include "HttpContext.h"
#include <algorithm>
#include <cctype>
#include <cstring>

std::string HttpContext::emptyString_;

static std::string toLower(const std::string &str) {
  std::string result = str;
  for (auto &c : result)
    c = std::tolower(c);
  return result;
}

bool HttpContext::parseRequest(const char *buf, size_t len) {
  // 将新数据追加到内部缓冲区
  buffer_.append(buf, len);

  const char *start = buffer_.data() + parsedBytes_;
  const char *end = buffer_.data() + buffer_.size();
  bool ok = true;

  while (start < end) {
    if (state_ == kExpectRequestLine) {
      const char *crlf = std::search(start, end, "\r\n", "\r\n" + 2);
      if (crlf != end) {
        ok = processRequestLine(start, crlf);
        if (ok) {
          start = crlf + 2;
          parsedBytes_ = start - buffer_.data();
          state_ = kExpectHeaders;
        } else {
          return false;
        }
      } else {
        break;
      }
    } else if (state_ == kExpectHeaders) {
      const char *crlf = std::search(start, end, "\r\n", "\r\n" + 2);
      if (crlf != end) {
        if (crlf == start) {
          start = crlf + 2;
          parsedBytes_ = start - buffer_.data();
          auto it = headers_.find("content-length");
          if (it != headers_.end()) {
            int contentLength = std::stoi(it->second);
            if (contentLength > 0) {
              state_ = kExpectBody;
            } else {
              state_ = kGotAll;
            }
          } else {
            state_ = kGotAll;
          }
        } else {
          ok = processHeaders(start, crlf);
          if (!ok)
            return false;
          start = crlf + 2;
          parsedBytes_ = start - buffer_.data();
        }
      } else {
        break;
      }
    } else if (state_ == kExpectBody) {
      auto it = headers_.find("content-length");
      if (it != headers_.end()) {
        size_t contentLength = std::stoul(it->second);
        size_t available = end - start;
        if (available >= contentLength) {
          body_.assign(start, start + contentLength);
          start += contentLength;
          parsedBytes_ = start - buffer_.data();
          state_ = kGotAll;
        } else {
          break;
        }
      } else {
        state_ = kGotAll;
      }
    }

    if (state_ == kGotAll) {
      return true;
    }
  }

  return true;
}

bool HttpContext::processRequestLine(const char *begin, const char *end) {
  const char *start = begin;
  const char *space = std::find(start, end, ' ');
  if (space == end)
    return false;

  std::string methodStr(start, space);
  if (methodStr == "GET")
    method_ = kGet;
  else if (methodStr == "POST")
    method_ = kPost;
  else if (methodStr == "HEAD")
    method_ = kHead;
  else
    return false;

  start = space + 1;
  space = std::find(start, end, ' ');
  if (space == end)
    return false;

  std::string uri(start, space);
  size_t questionMark = uri.find('?');
  if (questionMark != std::string::npos) {
    path_ = uri.substr(0, questionMark);
    query_ = uri.substr(questionMark + 1);
  } else {
    path_ = uri;
  }

  start = space + 1;
  version_.assign(start, end);
  return true;
}

bool HttpContext::processHeaders(const char *begin, const char *end) {
  const char *colon = std::find(begin, end, ':');
  if (colon == end)
    return false;

  std::string key(begin, colon);
  ++colon;
  while (colon < end && *colon == ' ')
    ++colon;
  std::string value(colon, end);

  headers_[toLower(key)] = value; // 统一存为小写
  return true;
}