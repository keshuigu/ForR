#define _GNU_SOURCE 1
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <set>

#define USER_LIMIT 5
#define BUFFER_SIZE 64
#define FD_LIMIT 65535
struct client_data {
  struct sockaddr_in address;
  char* write_buf;
  char read_buf[BUFFER_SIZE];
};

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

void delfd(int epfd, int fd) { epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr); }

void modfd(int epfd, int fd, uint32_t flag) {
  epoll_event e;
  e.data.fd = fd;
  e.events = flag;
  epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &e);
  // setnonblocking(fd);
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
  int listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(listenfd >= 0);
  int ret =
      bind(listenfd, (struct sockaddr*)&server_address, sizeof(server_address));
  assert(ret != -1);
  ret = listen(listenfd, 5);
  assert(ret != -1);
  // 足够大的空间存放客户端数据
  // 以连接的socketfd做索引
  client_data* users = new client_data[FD_LIMIT];

  // 记录所有描述符
  // int fds[USER_LIMIT + 1];
  // for (int i = 1; i <= USER_LIMIT; i++) {
  //   fds[i] = -1;
  // }
  // fds[0] = listenfd;

  epoll_event events[1024];
  int user_counter = 0;

  std::set<int> fds;

  int epfd = epoll_create(USER_LIMIT);
  addfd(epfd, listenfd, EPOLLIN | EPOLLERR | EPOLLET);

  while (true) {
    ret = epoll_wait(epfd, events, 1024, -1);
    if (ret < 0) {
      printf("epoll failed");
      break;
    }
    for (int i = 0; i < ret; i++) {
      if (events[i].data.fd == listenfd && (events[i].events & EPOLLIN)) {
        struct sockaddr_in client_address;
        socklen_t len = sizeof(client_address);
        int connfd = accept(listenfd, (struct sockaddr*)&client_address, &len);
        if (connfd < 0) {
          printf("errno is: %d\n", errno);
          continue;
        }
        if (user_counter >= USER_LIMIT) {
          const char* info = "too many users\n";
          printf("%s", info);
          send(connfd, info, strlen(info), 0);
          close(connfd);
          continue;
        }
        user_counter++;
        // 拷贝
        users[connfd].address = client_address;
        fds.emplace(connfd);
        // fds[user_counter] = connfd;
        addfd(epfd, connfd, EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET);
        printf("comes a new user \n");
      } else if (events[i].events & EPOLLERR) {
        printf("error from %d \n", i);
        char errors[100];
        memset(errors, '\0', 100);
        socklen_t length = sizeof(errors);
        if (getsockopt(events[i].data.fd, SOL_SOCKET, SO_ERROR, &errors,
                       &length)) {
          printf("get socket option failed\n");
        }
        printf("%s", errors);
        continue;
      } else if (events[i].events & EPOLLRDHUP) {
        // 关闭链接
        // 将后面的fd前移填空
        // users[events[i].data.fd] = users[fds[user_counter]];
        delfd(epfd, events[i].data.fd);
        close(events[i].data.fd);
        // fds[i] = fds[user_counter];
        fds.erase(fds.find(events[i].data.fd));
        user_counter--;
        printf("a client left\n");
      } else if (events[i].events & EPOLLIN) {
        int connfd = events[i].data.fd;
        memset(users[connfd].read_buf, '\0', BUFFER_SIZE);
        ret = recv(connfd, users[connfd].read_buf, BUFFER_SIZE - 1, 0);
        printf("get %d bytes %s from %d\n", ret, users[connfd].read_buf,
               connfd);
        if (ret <= 0) {
          if (errno != EAGAIN) {
            delfd(epfd, connfd);
            close(connfd);
            fds.erase(fds.find(events[i].data.fd));
            // users[fds[i].fd] = users[fds[user_counter].fd];
            // fds[i] = fds[user_counter];
            // i--;
            user_counter--;
          }
        } else {
          for (auto it = fds.begin(); it != fds.end(); ++it) {
            int curfd = *it;
            if (curfd == connfd) {
              continue;
            }
            modfd(epfd, curfd, EPOLLOUT);
            // fds[j].events &= ~POLLIN;
            // fds[j].events |= POLLOUT;
            users[curfd].write_buf = users[connfd].read_buf;
          }
        }
      } else if (events[i].events & EPOLLOUT) {
        int connfd = events[i].data.fd;
        if (!users[connfd].write_buf) {
          continue;
        }
        ret = send(connfd, users[connfd].write_buf,
                   strlen(users[connfd].write_buf), 0);
        users[connfd].write_buf = nullptr;
        modfd(epfd, connfd, EPOLLIN);
      }
    }
  }
  delete[] users;
  close(listenfd);
  return 0;
}