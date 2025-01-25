#define _GNU_SOURCE 1
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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
  pollfd fds[USER_LIMIT + 1];
  int user_counter = 0;
  for (int i = 1; i <= USER_LIMIT; i++) {
    fds[i].fd = -1;
    fds[i].events = 0;
    fds[i].revents = 0;
  }
  fds[0].fd = listenfd;
  fds[0].events = POLLIN | POLLERR;
  fds[0].revents = 0;

  while (true) {
    ret = poll(fds, user_counter + 1, -1);
    if (ret < 0) {
      printf("poll failed");
      break;
    }
    for (int i = 0; i < user_counter + 1; i++) {
      if (i == 0 && fds[i].revents & POLLIN) {
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
        setnonblocking(connfd);
        fds[user_counter].fd = connfd;
        fds[user_counter].events = POLLIN | POLLRDHUP | POLLERR;
        fds[user_counter].revents = 0;
        printf("comes a new user \n");
      } else if (fds[i].revents & POLLERR) {
        printf("error from %d \n", i);
        char errors[100];
        memset(errors, '\0', 100);
        socklen_t length = sizeof(errors);
        if (getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &errors, &length)) {
          printf("get socket option failed\n");
        }
        printf("%s", errors);
        continue;
      } else if (fds[i].revents & POLLRDHUP) {
        // 关闭链接
        // 将后面的fd前移填空
        users[fds[i].fd] = users[fds[user_counter].fd];
        close(fds[i].fd);
        fds[i] = fds[user_counter];
        i--;
        user_counter--;
        printf("a client left\n");
      } else if (fds[i].revents & POLLIN) {
        int connfd = fds[i].fd;
        memset(users[connfd].read_buf, '\0', BUFFER_SIZE);
        ret = recv(connfd, users[connfd].read_buf, BUFFER_SIZE - 1, 0);
        printf("get %d bytes %s from %d\n", ret, users[connfd].read_buf,
               connfd);
        if (ret < 0) {
          if (errno != EAGAIN) {
            close(connfd);
            users[fds[i].fd] = users[fds[user_counter].fd];
            fds[i] = fds[user_counter];
            i--;
            user_counter--;
          }
        } else if (ret > 0) {
          for (int j = 0; j <= user_counter; j++) {
            if (fds[j].fd == connfd) {
              continue;
            }
            fds[j].events &= ~POLLIN;
            fds[j].events |= POLLOUT;
            users[fds[j].fd].write_buf = users[connfd].read_buf;
          }
        }
      } else if (fds[i].revents & POLLOUT) {
        int connfd = fds[i].fd;
        if (!users[connfd].write_buf) {
          continue;
        }
        ret = send(connfd, users[connfd].write_buf,
                   strlen(users[connfd].write_buf), 0);
        users[connfd].write_buf = nullptr;
        fds[i].events &= ~POLLOUT;
        fds[i].events |= POLLIN;
      }
    }
  }
  delete[] users;
  close(listenfd);
  return 0;
}