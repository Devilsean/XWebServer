#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include "../lock/locker.h"
#include "../log/log.h"
#include <error.h>
#include <iostream>
#include <list>
#include <mutex>
#include <mysql/mysql.h>
#include <string>

using namespace std;

class connection_pool {
public:
  MYSQL *GetConnection();              //获取数据库连接
  bool ReleaseConnection(MYSQL *conn); //释放连接
  int GetFreeConn();                   //获取空闲连接数
  void DestroyPool();                  //销毁所有连接

  // 单例模式
  static connection_pool *GetInstance();

  // 单例模式禁用拷贝构造和赋值操作
  connection_pool(const connection_pool &) = delete;
  connection_pool &operator=(const connection_pool &) = delete;

  void init(string url, string User, string PassWord, string DataBaseName,
            int Port, int MaxConn, int close_log);

private:
  connection_pool();
  ~connection_pool();

  int m_MaxConn;          //最大连接数
  int m_CurConn;          //当前已使用的连接数
  int m_FreeConn;         //当前空闲的连接数
  locker m_lock;          //之前改好的locker类
  list<MYSQL *> connList; //连接池
  sem reserve;            // 信号量，保持复用

public:
  string m_url;          //主机地址
  string m_Port;         //数据库端口号
  string m_User;         //登陆数据库用户名
  string m_PassWord;     //登陆数据库密码
  string m_DatabaseName; //使用数据库名
  int m_close_log;       //日志开关
};

// 数据库连接的RAII封装类
class connectionRAII {
public:
  // 哥们，这里改成了更安全的构造
  connectionRAII(MYSQL **con, connection_pool *connPool);
  ~connectionRAII();

private:
  MYSQL *conRAII;
  connection_pool *poolRAII;
};

#endif