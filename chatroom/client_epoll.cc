#define _GNU_SOURCE 1
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
// #include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 64

int setnonblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}

void addfd(int epfd, int fd, uint32_t flag) {
  epoll_event e;
  e.data.fd = fd;
  e.events = flag;
  epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &e);
  setnonblocking(fd);
}

int main(int argc, char* argv[]) {
  const char* ip;
  int port;
  if (argc <= 2) {
    // printf("usage: %s ip_address port_number\n", basename(argv[0]));
    // return 1;
    ip = "localhost";
    port = 12346;
  } else {
    ip = argv[1];
    port = atoi(argv[2]);
  }

  struct sockaddr_in server_address;
  bzero(&server_address, sizeof(server_address));
  server_address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &server_address.sin_addr);
  server_address.sin_port = htons(port);

  int sockfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(sockfd >= 0);
  if (connect(sockfd, (struct sockaddr*)&server_address,
              sizeof(server_address)) < 0) {
    printf("connection failed");
    close(sockfd);
    return 1;
  }

  // pollfd fds[2];
  // fds[0].fd = STDIN_FILENO;
  // fds[0].events = POLLIN;
  // fds[0].revents = 0;

  // fds[1].fd = sockfd;
  // fds[1].events = POLLIN | POLLHUP;
  // fds[1].revents = 0;

  int epfd = epoll_create(2);
  assert(epfd >= 0);

  addfd(epfd, STDIN_FILENO, EPOLLIN | EPOLLET);
  addfd(epfd, sockfd, EPOLLIN | EPOLLHUP | EPOLLET);

  char read_buf[BUFFER_SIZE];
  int pipefd[2];
  int ret = pipe(pipefd);
  assert(ret != -1);
  epoll_event events[1024];
  while (true) {
    ret = epoll_wait(epfd, events, 1024, -1);
    if (ret < 0) {
      break;
    }
    for (int i = 0; i < ret; i++) {
      int fd = events[i].data.fd;
      if (fd == STDIN_FILENO && (events[i].events & EPOLLIN)) {
        ret = splice(fd, NULL, pipefd[1], NULL, 32768,
                     SPLICE_F_MORE | SPLICE_F_MOVE);
        assert(ret != -1);

        ret = splice(pipefd[0], NULL, sockfd, NULL, 32768,
                     SPLICE_F_MORE | SPLICE_F_MOVE);
        assert(ret != -1);
      }
      if (fd == sockfd && (events[i].events & EPOLLIN)) {
        while (true) {
          memset(read_buf, '\0', BUFFER_SIZE);
          ret = recv(sockfd, read_buf, BUFFER_SIZE - 1, 0);
          if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            }
            close(sockfd);
            break;
          } else if (ret == 0) {
            close(sockfd);
            break;
          } else {
            printf("%s", read_buf);
          }
        }
      } else if (fd == sockfd && (events[i].events & EPOLLHUP)) {
        printf("server closed\n");
        break;
      }
    }

    // if (fds[1].revents & POLLHUP) {
    //   printf("server closed");
    //   break;
    // } else if (fds[1].revents & POLLIN) {  // 电平触发，此处不需要循环
    //   memset(read_buf, '\0', BUFFER_SIZE);
    //   recv(fds[1].fd, read_buf, BUFFER_SIZE - 1, 0);
    //   printf("%s", read_buf);
    // }
    // if (fds[0].revents & POLLIN) {
    //   // splice 内核中移动数据
    //   // 电平触发，此处不需要循环
    //   ret = splice(fds[0].fd, NULL, pipefd[1], NULL, 32768,
    //                SPLICE_F_MORE | SPLICE_F_MOVE);
    //   assert(ret != -1);
    //   ret = splice(pipefd[0], NULL, fds[1].fd, NULL, 32768,
    //                SPLICE_F_MORE | SPLICE_F_MOVE);
    //   assert(ret != -1);
    // }
  }
  close(sockfd);
  close(epfd);
  return 0;
}