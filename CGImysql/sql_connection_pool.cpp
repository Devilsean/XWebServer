#include "sql_connection_pool.h"
#include <iostream>
#include <list>
#include <mutex> // 哥们，别忘了这个
#include <mysql/mysql.h>
#include <string>

using namespace std;

connection_pool::connection_pool() {
  m_CurConn = 0;
  m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance() {
  static connection_pool connPool;
  return &connPool;
}

void connection_pool::init(string url, string User, string PassWord,
                           string DBName, int Port, int MaxConn,
                           int close_log) {
  m_url = url;
  m_Port = to_string(Port); // 既然变量是 string，用 to_string 转换一下
  m_User = User;
  m_PassWord = PassWord;
  m_DatabaseName = DBName;
  m_close_log = close_log;

  for (int i = 0; i < MaxConn; i++) {
    MYSQL *con = nullptr;
    con = mysql_init(con);

    if (con == nullptr) {
      LOG_ERROR("MySQL Error");
      exit(1);
    }
    con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(),
                             DBName.c_str(), Port, nullptr, 0);

    if (con == nullptr) {
      LOG_ERROR("MySQL Error");
      exit(1);
    }
    connList.push_back(con);
    ++m_FreeConn;
  }

  // 信号量初始化
  reserve = sem(m_FreeConn);
  m_MaxConn = m_FreeConn;
}

MYSQL *connection_pool::GetConnection() {
  MYSQL *con = nullptr;

  if (0 == connList.size())
    return nullptr;

  reserve.wait(); // 等待信号量

  {
    // 使用 lock_guard 自动管理 m_lock
    std::lock_guard<std::mutex> locker(m_lock.get());
    con = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;
  }

  return con;
}

bool connection_pool::ReleaseConnection(MYSQL *con) {
  if (nullptr == con)
    return false;

  {
    std::lock_guard<std::mutex> locker(m_lock.get());
    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;
  }

  reserve.post();
  return true;
}

void connection_pool::DestroyPool() {
  std::lock_guard<std::mutex> locker(m_lock.get());
  if (connList.size() > 0) {
    // C++11 范围 for 循环
    for (auto con : connList) {
      mysql_close(con);
    }
    m_CurConn = 0;
    m_FreeConn = 0;
    connList.clear();
  }
}

int connection_pool::GetFreeConn() { return this->m_FreeConn; }

connection_pool::~connection_pool() { DestroyPool(); }

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
  *SQL = connPool->GetConnection();
  conRAII = *SQL;
  poolRAII = connPool;
}

connectionRAII::~connectionRAII() { poolRAII->ReleaseConnection(conRAII); }