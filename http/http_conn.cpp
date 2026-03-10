#include "http_conn.h"
#include "../CGImysql/sql_connection_pool.h"
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <mysql/mysql.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <unistd.h>

// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form =
    "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form =
    "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form =
    "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form =
    "There was an unusual problem serving the request file.\n";

// 静态变量初始化
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;
std::mutex m_lock;
std::map<std::string, std::string> users;

void http_conn::initmysql_result(connection_pool *connPool) {
  MYSQL *mysql = nullptr;
  connectionRAII mysqlcon(&mysql, connPool);

  if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
    LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    return;
  }

  MYSQL_RES *result = mysql_store_result(mysql);
  while (MYSQL_ROW row = mysql_fetch_row(result)) {
    users[row[0]] = row[1];
  }
  mysql_free_result(result);
}

// 设置非阻塞
int setnonblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, old_option | O_NONBLOCK);
  return old_option;
}

void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
  epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLRDHUP;
  if (1 == TRIGMode)
    event.events |= EPOLLET;
  if (one_shot)
    event.events |= EPOLLONESHOT;
  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
  epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
  close(fd);
}

void modfd(int epollfd, int fd, int ev, int TRIGMode) {
  epoll_event event;
  event.data.fd = fd;
  event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
  if (1 == TRIGMode)
    event.events |= EPOLLET;
  epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void http_conn::close_conn(bool real_close) {
  if (real_close && (m_sockfd != -1)) {
    removefd(m_epollfd, m_sockfd);
    m_sockfd = -1;
    m_user_count--;
  }
}

void http_conn::init(int sockfd, const sockaddr_in &addr, char *root,
                     int TRIGMode, int close_log, std::string user,
                     std::string passwd, std::string sqlname) {
  m_sockfd = sockfd;
  m_address = addr;
  addfd(m_epollfd, sockfd, true, TRIGMode);
  m_user_count++;

  doc_root = root;
  m_TRIGMode = TRIGMode;
  m_close_log = close_log;
  sql_user = std::move(user); // C++11 move 减少拷贝
  sql_passwd = std::move(passwd);
  sql_name = std::move(sqlname);

  init();
}

void http_conn::init() {
  mysql = nullptr;
  bytes_to_send = 0;
  bytes_have_send = 0;
  m_check_state = CHECK_STATE_REQUESTLINE;
  m_linger = false;
  m_method = GET;
  m_url = nullptr;
  m_version = nullptr;
  m_content_length = 0;
  m_host = nullptr;
  m_start_line = 0;
  m_checked_idx = 0;
  m_read_idx = 0;
  m_write_idx = 0;
  cgi = 0;
  m_state = 0;
  timer_flag = 0;
  improv = 0;

  // 使用 C++ 特性清理
  m_real_file.clear();
}

http_conn::LINE_STATUS http_conn::parse_line() {
  for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
    char temp = m_read_buf[m_checked_idx];
    if (temp == '\r') {
      if ((m_checked_idx + 1) == m_read_idx)
        return LINE_OPEN;
      if (m_read_buf[m_checked_idx + 1] == '\n') {
        m_read_buf[m_checked_idx++] = '\0';
        m_read_buf[m_checked_idx++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    } else if (temp == '\n') {
      if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
        m_read_buf[m_checked_idx - 1] = '\0';
        m_read_buf[m_checked_idx++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }
  }
  return LINE_OPEN;
}

bool http_conn::read_once() {
  if (m_read_idx >= READ_BUFFER_SIZE)
    return false;

  int bytes_read = 0;
  if (0 == m_TRIGMode) { // LT
    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                      READ_BUFFER_SIZE - m_read_idx, 0);
    if (bytes_read <= 0)
      return false;
    m_read_idx += bytes_read;
    return true;
  } else { // ET
    while (true) {
      bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                        READ_BUFFER_SIZE - m_read_idx, 0);
      if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;
        return false;
      }
      if (bytes_read == 0)
        return false;
      m_read_idx += bytes_read;
    }
    return true;
  }
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
  m_url = strpbrk(text, " \t");
  if (!m_url)
    return BAD_REQUEST;
  *m_url++ = '\0';

  if (strcasecmp(text, "GET") == 0)
    m_method = GET;
  else if (strcasecmp(text, "POST") == 0) {
    m_method = POST;
    cgi = 1;
  } else
    return BAD_REQUEST;

  m_url += strspn(m_url, " \t");
  m_version = strpbrk(m_url, " \t");
  if (!m_version)
    return BAD_REQUEST;
  *m_version++ = '\0';

  m_version += strspn(m_version, " \t");
  if (strcasecmp(m_version, "HTTP/1.1") != 0)
    return BAD_REQUEST;

  if (strncasecmp(m_url, "http://", 7) == 0) {
    m_url += 7;
    m_url = strchr(m_url, '/');
  } else if (strncasecmp(m_url, "https://", 8) == 0) {
    m_url += 8;
    m_url = strchr(m_url, '/');
  }

  if (!m_url || m_url[0] != '/')
    return BAD_REQUEST;
  m_check_state = CHECK_STATE_HEADER;
  return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
  if (text[0] == '\0') {
    return (m_content_length != 0)
               ? (m_check_state = CHECK_STATE_CONTENT, NO_REQUEST)
               : GET_REQUEST;
  }

  if (strncasecmp(text, "Connection:", 11) == 0) {
    text += 11;
    text += strspn(text, " \t");
    if (strcasecmp(text, "keep-alive") == 0)
      m_linger = true;
  } else if (strncasecmp(text, "Content-length:", 15) == 0) {
    text += 15;
    text += strspn(text, " \t");
    m_content_length = atol(text);
  } else if (strncasecmp(text, "Host:", 5) == 0) {
    text += 5;
    text += strspn(text, " \t");
    m_host = text;
  }
  return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text) {
  if (m_read_idx >= (m_content_length + m_checked_idx)) {
    text[m_content_length] = '\0';
    m_string = text;
    return GET_REQUEST;
  }
  return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read() {
  LINE_STATUS line_status = LINE_OK;
  HTTP_CODE ret = NO_REQUEST;
  char *text = nullptr;

  // 状态机逻辑：只有在解析内容且行完整，或者解析出一行 OK 时才继续
  while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
         ((line_status = parse_line()) == LINE_OK)) {

    // get_line 内部其实就是 return m_read_buf + m_start_line;
    text = get_line();

    // 此时 m_checked_idx 已经被 parse_line 挪到了下一行的开头
    // 但我们先不更新 m_start_line，保证 switch 里的解析逻辑使用的是当前行的text
    LOG_INFO("parsing line: %s", text);

    switch (m_check_state) {
    case CHECK_STATE_REQUESTLINE: {
      ret = parse_request_line(text);
      if (ret == BAD_REQUEST)
        return BAD_REQUEST;
      break;
    }
    case CHECK_STATE_HEADER: {
      ret = parse_headers(text);
      if (ret == BAD_REQUEST)
        return BAD_REQUEST;
      // 如果解析完头部发现是 GET 且没有消息体，直接 do_request
      else if (ret == GET_REQUEST)
        return do_request();
      break;
    }
    case CHECK_STATE_CONTENT: {
      ret = parse_content(text);
      if (ret == GET_REQUEST)
        return do_request();
      // 内容未读完，跳出循环等待后续 read 事件
      line_status = LINE_OPEN;
      break;
    }
    default:
      return INTERNAL_ERROR;
    }

    // 解析完当前行后，再同步起始位置到下一行开头
    m_start_line = m_checked_idx;
  }
  return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request() {
  m_real_file = doc_root;
  std::string url_str = m_url;

  // 如果URL只有'/'，默认跳转
  if (url_str == "/")
    url_str += "judge.html";

  const char *p = strrchr(m_url, '/');

  // CGI 业务逻辑重构
  if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
    std::string post_data = m_string;
    // 简单模拟解析: user=xxx&passwd=yyy
    auto u_pos = post_data.find("user=");
    auto p_pos = post_data.find("passwd=");
    auto amp = post_data.find('&');

    std::string name = post_data.substr(u_pos + 5, amp - (u_pos + 5));
    std::string password = post_data.substr(p_pos + 7);

    if (*(p + 1) == '3') { // 注册
      std::string sql_insert = "INSERT INTO user(username, passwd) VALUES('" +
                               name + "', '" + password + "')";
      std::lock_guard<std::mutex> lock(m_lock);
      if (users.find(name) == users.end()) {
        if (mysql_query(mysql, sql_insert.c_str()) == 0) {
          users[name] = password;
          url_str = "/log.html";
        } else
          url_str = "/registerError.html";
      } else
        url_str = "/registerError.html";
    } else { // 登录
      std::lock_guard<std::mutex> lock(m_lock);
      if (users.count(name) && users[name] == password)
        url_str = "/welcome.html";
      else
        url_str = "/logError.html";
    }
  }

  // 路由分发
  if (url_str[1] == '0')
    m_real_file += "/register.html";
  else if (url_str[1] == '1')
    m_real_file += "/log.html";
  else if (url_str[1] == '5')
    m_real_file += "/picture.html";
  else if (url_str[1] == '6')
    m_real_file += "/video.html";
  else if (url_str[1] == '7')
    m_real_file += "/file.html";
  else
    m_real_file += url_str;

  if (stat(m_real_file.c_str(), &m_file_stat) < 0)
    return NO_RESOURCE;
  if (!(m_file_stat.st_mode & S_IROTH))
    return FORBIDDEN_REQUEST;
  if (S_ISDIR(m_file_stat.st_mode))
    return BAD_REQUEST;

  int fd = open(m_real_file.c_str(), O_RDONLY);
  m_file_address =
      (char *)mmap(nullptr, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  return FILE_REQUEST;
}

void http_conn::unmap() {
  if (m_file_address) {
    munmap(m_file_address, m_file_stat.st_size);
    m_file_address = nullptr;
  }
}

bool http_conn::write() {
  if (bytes_to_send == 0) {
    modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
    init();
    return true;
  }

  while (true) {
    int temp = writev(m_sockfd, m_iv, m_iv_count);
    if (temp < 0) {
      if (errno == EAGAIN) {
        modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
        return true;
      }
      unmap();
      return false;
    }

    bytes_have_send += temp;
    bytes_to_send -= temp;

    // 更新 iovec 偏移，逻辑精简
    if ((size_t)bytes_have_send >= m_iv[0].iov_len) {
      m_iv[0].iov_len = 0;
      m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
      m_iv[1].iov_len = bytes_to_send;
    } else {
      m_iv[0].iov_base = m_write_buf + bytes_have_send;
      m_iv[0].iov_len -= bytes_have_send;
    }

    if (bytes_to_send <= 0) {
      unmap();
      modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
      if (m_linger) {
        init();
        return true;
      }
      return false;
    }
  }
}

bool http_conn::add_response(const char *format, ...) {
  if (m_write_idx >= WRITE_BUFFER_SIZE)
    return false;
  va_list arg_list;
  va_start(arg_list, format);
  int len = vsnprintf(m_write_buf + m_write_idx,
                      WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
  if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
    va_end(arg_list);
    return false;
  }
  m_write_idx += len;
  va_end(arg_list);
  LOG_INFO("response line added: %s", m_write_buf);
  return true;
}

bool http_conn::add_status_line(int status, const char *title) {
  return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
  return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
  return add_response("Content-Length:%d\r\n", content_len);
}

bool http_conn::add_linger() {
  return add_response("Connection:%s\r\n", m_linger ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() { return add_response("\r\n"); }

bool http_conn::add_content(const char *content) {
  return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret) {
  auto fill_error = [this](int status, const char *title, const char *form) {
    add_status_line(status, title);
    add_headers(strlen(form));
    return add_content(form);
  };

  switch (ret) {
  case INTERNAL_ERROR:
    return fill_error(500, error_500_title, error_500_form);
  case BAD_REQUEST:
    return fill_error(404, error_404_title, error_404_form);
  case FORBIDDEN_REQUEST:
    return fill_error(403, error_403_title, error_403_form);
  case FILE_REQUEST: {
    add_status_line(200, ok_200_title);
    if (m_file_stat.st_size != 0) {
      add_headers(m_file_stat.st_size);
      m_iv[0].iov_base = m_write_buf;
      m_iv[0].iov_len = m_write_idx;
      m_iv[1].iov_base = m_file_address;
      m_iv[1].iov_len = m_file_stat.st_size;
      m_iv_count = 2;
      bytes_to_send = m_write_idx + m_file_stat.st_size;
      return true;
    } else {
      const char *ok_string = "<html><body></body></html>";
      add_headers(strlen(ok_string));
      return add_content(ok_string);
    }
  }
  default:
    return false;
  }
}

void http_conn::process() {
  // 1. 调用上面的解析函数
  HTTP_CODE read_ret = process_read();

  // 如果 NO_REQUEST，说明数据还没读全（比如 ET 模式下缓冲区还没凑齐一个完整包）
  if (read_ret == NO_REQUEST) {
    // 必须重新注册 EPOLLIN，否则因为开启了 ONESHOT，后续数据进来也不会触发
    // epoll_wait
    modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
    return;
  }

  // 2. 根据解析结果生成响应（写缓冲区赋值、准备 iovec 等）
  bool write_ret = process_write(read_ret);
  if (!write_ret) {
    // 生成响应失败（如文件不存在等），直接关闭连接
    close_conn();
    return;
  }

  // 3. 注册写事件
  // 解析完成并准备好数据后，将事件切换为 EPOLLOUT，触发主循环中的 dealwithwrite
  modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}