
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

#include "httpconn.h"
#include "threadpool.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

void AddSig(int sig, void (*handler)(int), bool restart = true) {
  struct sigaction sa;
  memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = handler;
  if (restart) {
    sa.sa_flags |= SA_RESTART;
  }
  // handler执行时阻塞其他信号
  // 执行完毕才会接收其他信号
  sigfillset(&sa.sa_mask);
  assert(sigaction(sig, &sa, nullptr) != -1);
}

void ShowError(int connfd, const char* info) {
  std::printf("%s", info);
  send(connfd, info, strlen(info), 0);
  close(connfd);
}

int main(int argc, char* argv[]) {
  const char* ip = nullptr;
  int port = -1;
  if (argc <= 2) {
    // std::printf("usage: %s ip_address port_number_\n", basename(argv[0]));
    // return 1;
    ip = std::string("localhost").c_str();
    port = 12345;
  } else {
    ip = argv[1];
    int port = atoi(argv[2]);
  }

  AddSig(SIGPIPE, SIG_IGN);

  forr::ThreadPool<forr::http::HttpConn>* pool =
      new forr::ThreadPool<forr::http::HttpConn>;

  forr::http::HttpConn* users = new forr::http::HttpConn[MAX_FD];
  assert(users != nullptr);
  int user_cnt = 0;

  struct sockaddr_in server_address;
  bzero(&server_address, sizeof(server_address));
  server_address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &server_address.sin_addr);
  server_address.sin_port = htons(port);
  int listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(listenfd >= 0);
  linger tmp = {1, 0};
  setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
  int ret =
      bind(listenfd, (struct sockaddr*)&server_address, sizeof(server_address));
  assert(ret != -1);
  ret = listen(listenfd, 5);
  assert(ret != -1);

  epoll_event e[MAX_EVENT_NUMBER];
  int epollfd = epoll_create(5);
  assert(epollfd != -1);
  forr::http::AddFd(epollfd, listenfd, false);
  forr::http::HttpConn::epollfd_ = epollfd;

  while (true) {
    int number = epoll_wait(epollfd, e, MAX_EVENT_NUMBER, -1);
    if (number < 0 && (errno != EAGAIN && errno != EINTR)) {
      std::printf("epoll failed, errno:%d\n", errno);
      perror("epoll");
      break;
    }
    for (int i = 0; i < number; i++) {
      int sockfd = e[i].data.fd;
      if (sockfd == listenfd) {
        sockaddr_in client;
        socklen_t len = sizeof(client);
        int connfd = accept(listenfd, (sockaddr*)&client, &len);
        if (connfd < 0) {
          std::printf("errno is %d\n", errno);
          continue;
        }
        if (forr::http::HttpConn::user_cnt_ >= MAX_FD) {
          ShowError(connfd, "Internal server busy");
          continue;
        }
        users[connfd].Init(connfd, client);
      } else if (e[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        users[sockfd].CloseConn();
      } else if (e[i].events & EPOLLIN) {
        // read() 持续读到数据完全读出
        if (users[sockfd].Read()) {
          pool->Append(&users[sockfd]);
        } else {
          users[sockfd].CloseConn();
        }
      } else if (e[i].events & EPOLLOUT) {
        if (!users[sockfd].Write()) {
          users[sockfd].CloseConn();
        }
      }
    }
  }

  close(epollfd);
  close(listenfd);
  delete[] users;
  delete pool;
  return 0;
}