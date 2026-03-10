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

# 源代码列表
SOURCES = main.cpp \
          ./timer/lst_timer.cpp \
          ./http/http_conn.cpp \
          ./log/log.cpp \
          ./CGImysql/sql_connection_pool.cpp \
          webserver.cpp \
          config.cpp

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