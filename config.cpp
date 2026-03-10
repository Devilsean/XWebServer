#include "config.h"
#include <cstdlib> // 替代 stdlib.h
#include <unistd.h>

Config::Config() {
  // 这里不用写任何赋值了！
  // 因为在 config.h 里，我们已经用了 int PORT{9006}; 这种初始化
  // 除非你有复杂的计算逻辑，否则构造函数保持为空是最高级的
}

void Config::parse_arg(int argc, char *argv[]) {
  int opt;
  const char *str = "p:l:m:o:s:t:c:a:";

  // getopt 是 Linux 标准解析函数，逻辑已经很稳了
  while ((opt = getopt(argc, argv, str)) != -1) {
    switch (opt) {
    case 'p':
      PORT = std::atoi(optarg);
      break;
    case 'l':
      LOGWrite = std::atoi(optarg);
      break;
    case 'm':
      TRIGMode = std::atoi(optarg);
      break;
    case 'o':
      OPT_LINGER = std::atoi(optarg);
      break;
    case 's':
      sql_num = std::atoi(optarg);
      break;
    case 't':
      thread_num = std::atoi(optarg);
      break;
    case 'c':
      close_log = std::atoi(optarg);
      break;
    case 'a':
      actor_model = std::atoi(optarg);
      break;
    default:
      break;
    }
  }
}