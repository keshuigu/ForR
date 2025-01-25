
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "processpool.h"
namespace forr {
namespace {
constexpr int kBufferSize = 1024;
}  // namespace

class CGIConn {
 public:
  CGIConn() : epoll_fd_(-1) {}
  ~CGIConn() {}
  void Init(int epoll_fd, int sockfd, const struct sockaddr_in& client_addr) {
    epoll_fd_ = epoll_fd;
    sockfd_ = sockfd;
    address_ = client_addr;  // 值复制
    memset(buf_, '\0', kBufferSize);
    read_idx_ = 0;
  }

  void Process() {
    int idx = 0;
    int ret = -1;
    while (true) {
      idx = read_idx_;  // 下个字节起始位置
      ret =
          recv(sockfd_, buf_ + idx, kBufferSize - idx - 1, 0);  // 留c风格终止符
      if (ret < 0) {
        if (errno != EAGAIN) {
          RemoveFd(epoll_fd_, sockfd_);
        }
        break;
      } else if (ret == 0) {
        RemoveFd(epoll_fd_, sockfd_);
        break;
      } else {
        read_idx_ += ret;
        std::printf("user content is:%s\n", buf_);
        // 遇到\r\n处理请求
        for (; idx < read_idx_; idx++) {
          if (buf_[idx] == '\n') {
            break;
          }
        }
        if (idx == read_idx_) {
          continue;
        }
        buf_[idx] = '\0';
        const char* file_name = buf_;
        if (access(file_name, F_OK) == -1) {
          RemoveFd(epoll_fd_, sockfd_);
          break;
        }
        // 创建子进程以执行CGI
        ret = fork();
        if (ret == -1) {
          RemoveFd(epoll_fd_, sockfd_);
          break;
        } else if (ret > 0) {
          memset(buf_, '\0', kBufferSize);
          read_idx_ = 0;
          RemoveFd(epoll_fd_, sockfd_);
          break;
        } else {
          close(STDOUT_FILENO);
          dup(sockfd_);
          std::printf("cmd is:%s\n", buf_);
          // 第一个参数为path
          // 第二个参数为程序名
          // 第三个参数及后为所附加的参数
          // 以NULL结尾
          execl(buf_, buf_, nullptr);
          exit(0);
        }
      }
    }
  }

 private:
  int epoll_fd_;
  int sockfd_;
  struct sockaddr_in address_;
  char buf_[kBufferSize];
  int read_idx_;
};

}  // namespace forr

int main(int argc, char* argv[]) {
  if (argc <= 2) {
    std::printf("usage: %s ip_address port_number_\n", basename(argv[0]));
    return 1;
  }
  const char* ip = argv[1];
  int port = atoi(argv[2]);

  struct sockaddr_in server_address;
  bzero(&server_address, sizeof(server_address));
  server_address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &server_address.sin_addr);
  server_address.sin_port = htons(port);
  int listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(listenfd >= 0);
  int ret =
      bind(listenfd, (struct sockaddr*)&server_address, sizeof(server_address));
  assert(ret != -1);
  ret = listen(listenfd, 5);
  assert(ret != -1);

  forr::ProcessPool<forr::CGIConn>* pool =
      forr::ProcessPool<forr::CGIConn>::Create(listenfd);
  if (pool) {
    pool->Run();
    pool->Destory();
  }
  close(listenfd);
  return 0;
}