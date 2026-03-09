#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <map>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../CGImysql/sql_connection_pool.h"
#include "../lock/locker.h"
#include "../log/log.h"
#include "../timer/lst_timer.h"

class http_conn {
public:
  static const int FILENAME_LEN = 200;
  static const int READ_BUFFER_SIZE = 2048;
  static const int WRITE_BUFFER_SIZE = 1024;
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
  http_conn() {}
  ~http_conn() {}

public:
  void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user,
            string passwd, string sqlname);
  void close_conn(bool real_close = true);
  void process();
  bool read_once();
  bool write();
  sockaddr_in *get_address() { return &m_address; }
  void initmysql_result(connection_pool *connPool);
  int timer_flag;
  int improv; // 用于判断是否处理完HTTP请求

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
  MYSQL *mysql;
  int m_state; //读为0, 写为1

private:
  // 网络连接相关
  int m_sockfd;          // 套接字文件描述符
  sockaddr_in m_address; // 客户端地址信息

  // HTTP请求处理
  char m_read_buf[READ_BUFFER_SIZE]; // 读缓冲区
  long m_read_idx;                   // 已读字节数
  long m_checked_idx;                // 已解析字节数
  int m_start_line;                  // 行起始位置
  CHECK_STATE m_check_state;         // 解析状态机状态
  METHOD m_method;                   // HTTP方法（GET/POST等）

  // HTTP请求解析
  char *m_url;           // 请求URL
  char *m_version;       // HTTP版本
  char *m_host;          // 主机名
  long m_content_length; // 内容长度
  bool m_linger;         // 是否保持连接
  char *m_string;        // 请求头数据
  int cgi;               // CGI标志（是否启用POST）

  // HTTP响应处理
  char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
  int m_write_idx;                     // 已写字节数
  struct iovec m_iv[2];                // 分散写向量
  int m_iv_count;                      // 向量数量
  int bytes_to_send;                   // 待发送字节数
  int bytes_have_send;                 // 已发送字节数

  // 文件处理相关
  char m_real_file[FILENAME_LEN]; // 实际文件路径
  char *m_file_address;           // 文件内存映射地址
  struct stat m_file_stat;        // 文件状态信息
  char *doc_root;                 // 文档根目录

  // 数据库相关
  map<string, string> m_users; // 用户信息映射
  char sql_user[100];          // 数据库用户名
  char sql_passwd[100];        // 数据库密码
  char sql_name[100];          // 数据库名

  // 配置和模式
  int m_TRIGMode;  // 触发模式
  int m_close_log; // 日志开关
};

#endif
