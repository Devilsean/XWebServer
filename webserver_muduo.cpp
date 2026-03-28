#include "webserver_muduo.h"
#include "./log/log.h"
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <mysql/mysql.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

WebServerMuduo::WebServerMuduo(EventLoop *loop, const InetAddress &listenAddr,
                               const std::string &name)
    : loop_(loop), connPool_(nullptr), sqlNum_(0), logWrite_(0), closeLog_(0),
      threadNum_(0) {
  // 获取文档根目录
  char server_path[PATH_MAX];
  if (getcwd(server_path, sizeof(server_path)) != nullptr) {
    docRoot_ = std::string(server_path) + "/root";
  }

  // 创建 HTTP 服务器
  server_ = std::make_unique<HttpServer>(loop, listenAddr, name);
  server_->setHttpCallback(std::bind(&WebServerMuduo::onRequest, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2));
}

WebServerMuduo::~WebServerMuduo() {}

void WebServerMuduo::init(std::string user, std::string passWord,
                          std::string databaseName, int log_write,
                          int close_log, int sql_num, int thread_num) {
  sqlUser_ = std::move(user);
  sqlPasswd_ = std::move(passWord);
  sqlName_ = std::move(databaseName);
  sqlNum_ = sql_num;
  logWrite_ = log_write;
  closeLog_ = close_log;
  threadNum_ = thread_num;

  // 初始化日志
  if (closeLog_ == 0) {
    int split_num = (logWrite_ == 1) ? 800000 : 0;
    Log::get_instance()->init("./ServerLog", closeLog_, 2000, 800000,
                              split_num);
  }

  // 初始化数据库连接池
  initSqlPool();

  // 加载用户数据
  loadUsers();
}

void WebServerMuduo::setThreadNum(int numThreads) {
  server_->setThreadNum(numThreads);
}

void WebServerMuduo::start() { server_->start(); }

void WebServerMuduo::initSqlPool() {
  try {
    connPool_ = connection_pool::GetInstance();
    connPool_->init("localhost", sqlUser_, sqlPasswd_, sqlName_, 3306, sqlNum_,
                    closeLog_);
    if (closeLog_ == 0) {
      Log::get_instance()->write_log(
          1, "Database connection pool initialized successfully");
      Log::get_instance()->flush();
    }
  } catch (const std::exception &e) {
    if (closeLog_ == 0) {
      Log::get_instance()->write_log(
          3, "Failed to initialize database connection pool: %s", e.what());
      Log::get_instance()->flush();
    }
    // 不抛出异常，允许服务器继续运行（虽然没有数据库功能）
    connPool_ = nullptr;
  }
}

void WebServerMuduo::loadUsers() {
  loadUsersThread_ = std::thread([this]() {
    if (connPool_ == nullptr) {
      Log::get_instance()->write_log(
          3,
          "Database connection pool is not initialized, skipping user loading");
      Log::get_instance()->flush();
      usersLoaded_.store(true);
      usersLoadedCV_.notify_all();
      return;
    }

    MYSQL *mysql = nullptr;
    connectionRAII mysqlcon(&mysql, connPool_);

    if (mysql == nullptr) {
      Log::get_instance()->write_log(
          3, "Failed to get database connection for loading users");
      Log::get_instance()->flush();
      usersLoaded_.store(true);
      usersLoadedCV_.notify_all();
      return;
    }

    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
      Log::get_instance()->write_log(3, "Failed to query users: %s",
                                     mysql_error(mysql));
      Log::get_instance()->flush();
      usersLoaded_.store(true);
      usersLoadedCV_.notify_all();
      return;
    }

    MYSQL_RES *result = mysql_store_result(mysql);
    if (result == nullptr) {
      Log::get_instance()->write_log(3, "Failed to store result: %s",
                                     mysql_error(mysql));
      Log::get_instance()->flush();
      usersLoaded_.store(true);
      usersLoadedCV_.notify_all();
      return;
    }

    int user_count = 0;
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
      std::lock_guard<std::mutex> lock(usersMutex_);
      users_[row[0]] = row[1];
      user_count++;
    }
    mysql_free_result(result);

    Log::get_instance()->write_log(1, "Loaded %d users from database",
                                   user_count);
    Log::get_instance()->flush();

    usersLoaded_.store(true);
    usersLoadedCV_.notify_all();
  });
  loadUsersThread_.detach();
}

// 修正后的核心回调函数
void WebServerMuduo::onRequest(const HttpContext &req, HttpResponse *resp) {
  if (req.method() == HttpContext::kPost) {
    handleCGI(req, resp);
  } else {
    handleStaticFile(req, resp);
  }
}

// 处理 CGI 请求（登录/注册）
void WebServerMuduo::handleCGI(const HttpContext &req, HttpResponse *resp) {
  std::string path = req.path();
  std::string body = req.body();

  // 处理 judge.html 的表单按钮跳转 (GET 风格逻辑在 POST 里的兼容)
  // 这些只是简单跳转，不需要数据库操作，直接处理避免延迟
  if (path == "/0") {
    handleStaticFile(req, resp); // 内部会自动映射到 register.html
    return;
  } else if (path == "/1") {
    handleStaticFile(req, resp); // 内部会自动映射到 log.html
    return;
  } else if (path == "/5" || path == "/6" || path == "/7") {
    handleStaticFile(req, resp); // 内部会自动映射到对应页面
    return;
  }

  // 对于真正的 CGI 请求（登录/注册），需要等待用户数据加载
  if (!usersLoaded_.load()) {
    std::unique_lock<std::mutex> lock(usersMutex_);
    usersLoadedCV_.wait_for(lock, std::chrono::seconds(5),
                            [this] { return usersLoaded_.load(); });
  }

  // 解析 POST 参数
  std::string username, password;
  size_t userPos = body.find("user=");
  size_t passwdPos = body.find("password=");
  size_t ampPos = body.find('&');

  if (userPos != std::string::npos && passwdPos != std::string::npos &&
      ampPos != std::string::npos) {
    username = body.substr(userPos + 5, ampPos - (userPos + 5));
    password = body.substr(passwdPos + 9, body.length() - (passwdPos + 9));
  }

  if (username.empty() || password.empty()) {
    resp->setStatusCode(HttpResponse::k400BadRequest);
    resp->setContentType("text/html");
    resp->setBody("<html><body><h1>Invalid Input</h1></body></html>");
    return;
  }

  std::string redirectPath;
  if (path == "/3CGISQL.cgi" || path == "/3") { // 注册
    MYSQL *mysql = nullptr;
    connectionRAII mysqlcon(&mysql, connPool_);

    if (mysql == nullptr) {
      Log::get_instance()->write_log(
          3, "Failed to get database connection for registration");
      Log::get_instance()->flush();
      redirectPath = "/registerError.html";
    } else {
      bool userExists = false;
      {
        std::lock_guard<std::mutex> lock(usersMutex_);
        userExists = (users_.find(username) != users_.end());
      }

      if (!userExists) {
        MYSQL_STMT *stmt = mysql_stmt_init(mysql);
        if (stmt == nullptr) {
          Log::get_instance()->write_log(
              3, "Failed to initialize statement: %s", mysql_error(mysql));
          Log::get_instance()->flush();
          redirectPath = "/registerError.html";
        } else {
          const char *query = "INSERT INTO user(username, passwd) VALUES(?, ?)";
          if (mysql_stmt_prepare(stmt, query, strlen(query))) {
            Log::get_instance()->write_log(3, "Failed to prepare statement: %s",
                                           mysql_stmt_error(stmt));
            Log::get_instance()->flush();
            redirectPath = "/registerError.html";
          } else {
            MYSQL_BIND bind[2];
            memset(bind, 0, sizeof(bind));

            unsigned long username_len = username.length();
            unsigned long password_len = password.length();

            bind[0].buffer_type = MYSQL_TYPE_VAR_STRING;
            bind[0].buffer = (char *)username.c_str();
            bind[0].buffer_length = username.length() + 1;
            bind[0].length = &username_len;

            bind[1].buffer_type = MYSQL_TYPE_VAR_STRING;
            bind[1].buffer = (char *)password.c_str();
            bind[1].buffer_length = password.length() + 1;
            bind[1].length = &password_len;

            if (mysql_stmt_bind_param(stmt, bind)) {
              Log::get_instance()->write_log(3, "Failed to bind parameters: %s",
                                             mysql_stmt_error(stmt));
              Log::get_instance()->flush();
              redirectPath = "/registerError.html";
            } else if (mysql_stmt_execute(stmt)) {
              Log::get_instance()->write_log(
                  3, "Failed to execute statement: %s", mysql_stmt_error(stmt));
              Log::get_instance()->flush();
              redirectPath = "/registerError.html";
            } else {
              {
                std::lock_guard<std::mutex> lock(usersMutex_);
                users_[username] = password;
              }
              redirectPath = "/log.html";
            }
            mysql_stmt_reset(stmt);
          }
          mysql_stmt_close(stmt);
        }
      } else {
        redirectPath = "/registerError.html";
      }
    }
  } else if (path == "/2CGISQL.cgi" || path == "/2") { // 登录
    // 优先使用内存缓存验证，减少数据库压力
    bool loginSuccess = false;
    {
      std::lock_guard<std::mutex> lock(usersMutex_);
      auto it = users_.find(username);
      if (it != users_.end()) {
        loginSuccess = (it->second == password);
      }
    }

    if (loginSuccess) {
      redirectPath = "/welcome.html";
    } else {
      redirectPath = "/logError.html";
    }
  }

  // 执行跳转
  if (!redirectPath.empty()) {
    HttpContext fakeReq;
    fakeReq.setPath(redirectPath);
    handleStaticFile(fakeReq, resp);
  } else {
    // 如果没有设置跳转路径，返回404错误
    resp->setStatusCode(HttpResponse::k404NotFound);
    resp->setContentType("text/html");
    resp->setBody("<html><body><h1>404 Not Found</h1></body></html>");
  }
}

// 处理静态文件 - 对应 http_conn::do_request 的静态文件部分
void WebServerMuduo::handleStaticFile(const HttpContext &req,
                                      HttpResponse *resp) {
  std::string path = req.path();

  // 1. 路由映射（对应 http_conn.cpp:312-361）
  if (path == "/" || path == "/judge.html")
    path = "/judge.html";
  else if (path == "/0")
    path = "/register.html";
  else if (path == "/1")
    path = "/log.html";
  else if (path == "/5")
    path = "/picture.html";
  else if (path == "/6")
    path = "/video.html";
  else if (path == "/7")
    path = "/file.html";

  // 2. 检查文件（对应 http_conn.cpp:363-368）
  std::string filePath = docRoot_ + path;
  struct stat fileStat;
  if (stat(filePath.c_str(), &fileStat) < 0) {
    resp->setStatusCode(HttpResponse::k404NotFound);
    resp->setContentType("text/html");
    resp->setBody("<html><body><h1>404 Not Found</h1></body></html>");
    return;
  }

  // 3. 权限检查（对应 http_conn.cpp:365）
  if (!(fileStat.st_mode & S_IROTH)) {
    resp->setStatusCode(HttpResponse::k403Forbidden);
    resp->setContentType("text/html");
    resp->setBody("<html><body><h1>403 Forbidden</h1></body></html>");
    return;
  }

  // 4. 禁止访问目录（对应 http_conn.cpp:367）
  if (S_ISDIR(fileStat.st_mode)) {
    resp->setStatusCode(HttpResponse::k400BadRequest);
    resp->setContentType("text/html");
    resp->setBody("<html><body><h1>400 Bad Request</h1></body></html>");
    return;
  }

  int fd = open(filePath.c_str(), O_RDONLY);
  if (fd < 0) {
    resp->setStatusCode(HttpResponse::k403Forbidden);
    return;
  }

  // 5. 设置响应（使用 sendfile 零拷贝，fd 由 HttpServer 关闭）
  resp->setStatusCode(HttpResponse::k200Ok);
  resp->setFile(fd, fileStat.st_size);

  // 7. Content-Type 识别（对应 http_conn 的 MIME 类型）
  size_t dotPos = path.find_last_of('.');
  std::string ext =
      (dotPos == std::string::npos) ? "" : path.substr(dotPos + 1);
  if (ext == "html" || ext == "htm")
    resp->setContentType("text/html");
  else if (ext == "jpg" || ext == "jpeg")
    resp->setContentType("image/jpeg");
  else if (ext == "png")
    resp->setContentType("image/png");
  else if (ext == "gif")
    resp->setContentType("image/gif");
  else if (ext == "css")
    resp->setContentType("text/css");
  else if (ext == "js")
    resp->setContentType("application/javascript");
  else if (ext == "mp4")
    resp->setContentType("video/mp4");
  else
    resp->setContentType("application/octet-stream");

  // 8. Keep-Alive 逻辑（对应 http_conn.cpp:418-421）
  bool keepAlive =
      (req.getHeader("connection") == "keep-alive") ||
      (req.version() == "HTTP/1.1" && req.getHeader("connection") != "close");
  resp->setCloseConnection(!keepAlive);
}