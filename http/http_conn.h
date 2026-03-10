#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <map>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/stat.h>
#include <sys/uio.h>

// 前置声明：减少头文件包含，加快编译速度
class connection_pool;
struct MYSQL;

class http_conn {
public:
  // 使用 C++11 constexpr，类型安全且性能好
  static constexpr int READ_BUFFER_SIZE = 2048;
  static constexpr int WRITE_BUFFER_SIZE = 1024;
  static constexpr int FILENAME_LEN = 200;

  // 状态枚举保持现状
  enum METHOD {
    GET = 0,
    POST,
    HEAD,
    PUT,
    DELETE,
    TRACE,
    OPTIONS,
    CONNECT,
    PATH
  };
  enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
  };
  enum HTTP_CODE {
    NO_REQUEST,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION
  };
  enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
  http_conn() = default;
  ~http_conn() = default;

  // C++11 禁用拷贝
  http_conn(const http_conn &) = delete;
  http_conn &operator=(const http_conn &) = delete;

public:
  // 接口优化：使用 std::string 传递参数
  void init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
            int close_log, std::string user, std::string passwd,
            std::string sqlname);
  void close_conn(bool real_close = true);
  void process();
  bool read_once();
  bool write();
  sockaddr_in *get_address() { return &m_address; }
  void initmysql_result(connection_pool *connPool);

  int timer_flag = 0;
  int improv = 0;

private:
  void init();
  HTTP_CODE process_read();
  bool process_write(HTTP_CODE ret);
  HTTP_CODE parse_request_line(char *text);
  HTTP_CODE parse_headers(char *text);
  HTTP_CODE parse_content(char *text);
  HTTP_CODE do_request();
  char *get_line() { return m_read_buf + m_start_line; };
  LINE_STATUS parse_line();
  void unmap();

  // 响应构建
  bool add_response(const char *format, ...);
  bool add_content(const char *content);
  bool add_status_line(int status, const char *title);
  bool add_headers(int content_length);
  bool add_content_type();
  bool add_content_length(int content_length);
  bool add_linger();
  bool add_blank_line();

public:
  static int m_epollfd;
  static int m_user_count;
  MYSQL *mysql = nullptr;
  int m_state = 0; // 0:读, 1:写

private:
  int m_sockfd = -1;
  sockaddr_in m_address{};

  // 缓冲区：使用 {} 初始化全 0
  char m_read_buf[READ_BUFFER_SIZE]{};
  long m_read_idx = 0;
  long m_checked_idx = 0;
  int m_start_line = 0;

  // 写缓冲区
  char m_write_buf[WRITE_BUFFER_SIZE]{};
  int m_write_idx = 0;

  // 状态机
  CHECK_STATE m_check_state = CHECK_STATE_REQUESTLINE;
  METHOD m_method = GET;

  // 解析结果：尽量使用原始指针指向缓冲区，避免额外内存拷贝
  char *m_url = nullptr;
  char *m_version = nullptr;
  char *m_host = nullptr;
  long m_content_length = 0;
  bool m_linger = false;
  char *m_file_address = nullptr;
  struct stat m_file_stat {};
  struct iovec m_iv[2]{};
  int m_iv_count = 0;
  int bytes_to_send = 0;
  int bytes_have_send = 0;

  // 配置信息：全部改用现代字符串
  std::string m_real_file;
  std::string doc_root;
  std::string sql_user;
  std::string sql_passwd;
  std::string sql_name;

  int m_TRIGMode = 0;
  int m_close_log = 0;
  char *m_string = nullptr; // 存储 POST 数据
  int cgi = 0;              // 是否启用 POST
};

#endif