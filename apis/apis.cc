#include <arpa/inet.h>   // inet_pton, inet_ntop 等函数
#include <netinet/in.h>  // sockaddr_in 结构体
#include <sys/socket.h>  // socket 函数及数据结构
#include <sys/types.h>   // 数据类型定义
#include <unistd.h>      // close 函数

#include <cstring>   // memset, memcpy 等函数
#include <iostream>  // 标准输入输出流

namespace apis {
void printbytes(const char* s, int n) {
  for (int i = 0; i < n; i++) {
    std::cout << s[i];
  }
  std::cout << '\n';
};

void byteorder() {
  union {
    short value;
    char union_bytes[sizeof(short)];
  } test;

  test.value = 0x4849;
  printbytes(test.union_bytes, sizeof(short));
  if ((test.union_bytes[0] == 0x48) && (test.union_bytes[1] == 0x49)) {
    std::cout << "big\n";
  } else {
    std::cout << "little\n";
  }
}

void bytetrans() {
  union {
    uint16_t value;
    char union_bytes[sizeof(uint16_t)];
  } test_short;
  union {
    uint32_t value;
    char union_bytes[sizeof(uint32_t)];
  } test_long;

  test_short.value = 0x4849;
  printbytes(test_short.union_bytes, sizeof(uint16_t));
  test_short.value = htons(test_short.value);
  printbytes(test_short.union_bytes, sizeof(uint16_t));
  test_short.value = ntohs(test_short.value);
  printbytes(test_short.union_bytes, sizeof(uint16_t));

  test_long.value = 0x48494a4b;
  printbytes(test_long.union_bytes, sizeof(uint32_t));
  test_long.value = htonl(test_long.value);
  printbytes(test_long.union_bytes, sizeof(uint32_t));
  test_long.value = ntohl(test_long.value);
  printbytes(test_long.union_bytes, sizeof(uint32_t));
}

void printsocket() {
  struct sockaddr_storage s;
  std::cout << s.ss_family << '\n';
}

}  // namespace apis

int main() {
  apis::bytetrans();
  return 0;
}
