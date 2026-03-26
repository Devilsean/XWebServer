CXX ?= g++

# 默认开启调试模式
DEBUG ?= 1

# 基础编译参数
# 1. 升级为 -std=c++14 
# 2. -Wall 开启所有警告，防止低级错误
# 3. -O 优化在非调试模式下开启
CXXFLAGS = -Wall -lpthread -lmysqlclient

ifeq ($(DEBUG), 1)
    CXXFLAGS += -g -std=c++14
else
    CXXFLAGS += -O3 -std=c++14
endif

# Muduo 核心源代码
MUDUO_SRCS = ./muduo/src/Acceptor.cc \
             ./muduo/src/Buffer.cc \
             ./muduo/src/Channel.cc \
             ./muduo/src/CurrentThread.cc \
             ./muduo/src/DefaultPoller.cc \
             ./muduo/src/EPollPoller.cc \
             ./muduo/src/EventLoop.cc \
             ./muduo/src/EventLoopThread.cc \
             ./muduo/src/EventLoopThreadPool.cc \
             ./muduo/src/InetAddress.cc \
             ./muduo/src/Logger.cc \
             ./muduo/src/Poller.cc \
             ./muduo/src/Socket.cc \
             ./muduo/src/TcpConnection.cc \
             ./muduo/src/TcpServer.cc \
             ./muduo/src/Thread.cc \
             ./muduo/src/Timestamp.cc

# HTTP 层源代码
HTTP_SRCS = ./http/HttpContext.cpp \
            ./http/HttpServer.cpp

# 项目源代码
SOURCES = main.cpp \
          ./log/log.cpp \
          ./CGImysql/sql_connection_pool.cpp \
          ./webserver_muduo.cpp \
          ./config.cpp \
          $(MUDUO_SRCS) \
          $(HTTP_SRCS)

# 目标文件
TARGET = server

# 核心编译规则
$(TARGET): $(SOURCES)
	$(CXX) -o $(TARGET) $^ $(CXXFLAGS)

# 清理
clean:
	rm -f $(TARGET)

# 伪目标防止同名文件冲突
.PHONY: clean