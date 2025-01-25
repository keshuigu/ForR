#include <arpa/inet.h>  // inet_pton, inet_ntop 等函数
#include <fcntl.h>
#include <netdb.h>       // gethostbyname
#include <netinet/in.h>  // sockaddr_in 结构体
#include <signal.h>      // 中断
#include <sys/socket.h>  // socket 函数及数据结构
#include <sys/stat.h>
#include <sys/types.h>  // 数据类型定义
#include <sys/un.h>     // struct sockeraddr_un
#include <unistd.h>     // close 函数

#include <cassert>
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
  struct sockaddr_storage s;  // 通用
  struct sockaddr_un saun;    // unix 本地专用
  struct sockaddr_in sain;    // ipv4
  std::cout << s.ss_family << '\n';
}

void printaddr() {
  in_addr_t addr = inet_addr("1.1.1.1");
  std::cout << addr << '\n';
  struct in_addr s;
  std::cout << inet_aton("2.2.2.2", &s) << '\n';
  std::cout << s.s_addr << '\n';
}

static bool stop = false;
void handle_term(int sig) { stop = true; }

void sockets() {
  signal(SIGTERM, handle_term);
  std::cout << "test sockets\n";
  int sd = socket(PF_INET, SOCK_STREAM, 0);
  struct sockaddr_in sain;  // ipv4
  inet_aton("127.0.0.1", &sain.sin_addr);
  sain.sin_port = htons(22334);  // 注意用转换函数转换字节序
  sain.sin_family = PF_INET;
  int err = bind(sd, (sockaddr*)&sain, sizeof(sain));
  assert(err != -1);
  err = listen(sd, 5);
  assert(err != -1);
  struct sockaddr_in client;
  socklen_t client_addrlength = sizeof(client);
  int connfd = accept(sd, (sockaddr*)&client, &client_addrlength);
  if (connfd < 0) {
    std::cout << errno << '\n';
  } else {
    char remote[INET_ADDRSTRLEN];
    std::cout << inet_ntop(AF_INET, &client.sin_addr, remote, INET_ADDRSTRLEN)
              << ' ' << ntohs(client.sin_port);
  }
  close(sd);
}

void options() {
  // int sock = socket(PF_INET, SOCK_STREAM, 0);
  // int reuse = 1;
  // // SOL_SOCKET 选项所属的level
  // setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  // struct sockaddr_in addr;
  // bzero(&addr, sizeof(addr));
  // addr.sin_family = AF_INET;
  // inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  // addr.sin_port = htons(12345);
  // int ret = bind(sock, (struct sockaddr*)&addr, sizeof(addr));

  int sockfd;
  int sendbuf = 3000;
  // 根据https://blog.csdn.net/mabin2005/article/details/126062360
  // 设置这玩意的意义不大
  socklen_t optlen;
  int res;

  // 创建一个 TCP 套接字
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  // 设置发送缓冲区大小
  res = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(sendbuf));
  if (res < 0) {
    perror("setsockopt");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  // 获取并打印发送缓冲区大小
  optlen = sizeof(sendbuf);
  res = getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuf, &optlen);
  if (res < 0) {
    perror("getsockopt");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  printf("发送缓冲区大小: %d\n", sendbuf);

  // 关闭套接字
  close(sockfd);
}

void hostname() {
  struct hostent* res = gethostbyname("www.baidu.com");
  std::cout << res->h_name << res->h_addrtype << res->h_length;
  struct servent* ress = getservbyname("daytime", NULL);
  std::cout << ress->s_name;
}

void pipes() {
  int fd[2];
  // fd[1] = open("read.txt",O_CREAT|O_RDONLY);
  // fd[2] = open("write.txt",O_CREAT|O_WRONLY);
  pipe(fd);
  char wbuf[100];
  const char* s = "hello";
  strncpy(wbuf, s, strlen(s));
  write(fd[1], wbuf, strlen(s));
  char rbuf[100];
  read(fd[0], rbuf, strlen(s));
  std::cout << rbuf;
  close(fd[0]);
  close(fd[1]);
}

void simpleCGI() {
  const char* ip = "127.0.0.1";
  int port = 12345;

  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &address.sin_addr);
  address.sin_port = htons(port);

  int sock = socket(PF_INET, SOCK_STREAM, 0);
  assert(sock > 0);
  int err = bind(sock, (struct sockaddr*)&address, sizeof(address));
  assert(err != -1);
  err = listen(sock, 5);
  assert(err != -1);

  // fd 是进程级资源
  struct sockaddr_in client;
  socklen_t client_length = sizeof(client);
  int connfd = accept(sock, (struct sockaddr*)&client, &client_length);
  assert(connfd > 0);
  close(STDOUT_FILENO);
  dup(connfd);
  std::cout << "testcgi" << '\n';
  close(connfd);
  close(sock);
}

void simpleEcho() {
  const char* ip = "127.0.0.1";
  int port = 12345;

  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &address.sin_addr);
  address.sin_port = htons(port);

  int sock = socket(PF_INET, SOCK_STREAM, 0);
  assert(sock > 0);
  int err = bind(sock, (struct sockaddr*)&address, sizeof(address));
  assert(err != -1);
  err = listen(sock, 5);
  assert(err != -1);

  struct sockaddr_in client;
  socklen_t client_length = sizeof(client);
  int connfd = accept(sock, (struct sockaddr*)&client, &client_length);
  assert(connfd > 0);
  int pipefd[2];
  err = pipe(pipefd);
  assert(err != -1);
  // 未实现监听链接断开
  while (true) {
    err = splice(connfd, NULL, pipefd[1], NULL, 32768,
                 SPLICE_F_MORE | SPLICE_F_MOVE);
    assert(err != -1);
    err = splice(pipefd[0], NULL, connfd, NULL, 32768,
                 SPLICE_F_MORE | SPLICE_F_MOVE);
  }
  close(connfd);
  close(sock);
}

void testUid() {
  uid_t uid = getuid();
  uid_t euid = geteuid();
  std::cout << uid << ' ' << euid << '\n';
}

}  // namespace apis

int main() {
  // apis::bytetrans();
  // apis::printaddr();
  // apis::sockets();
  // apis::options();
  // apis::hostname();
  // apis::pipes();
  // apis::simpleCGI();
  // apis::simpleEcho();
  apis::testUid();
  return 0;
}
