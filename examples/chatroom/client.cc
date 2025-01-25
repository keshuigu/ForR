#define _GNU_SOURCE 1
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 64

int main(int argc, char* argv[]) {
  if (argc <= 2) {
    printf("usage: %s ip_address port_number\n", basename(argv[0]));
    return 1;
  }

  const char* ip = argv[1];
  int port = atoi(argv[2]);

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
  pollfd fds[2];
  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;
  fds[0].revents = 0;

  fds[1].fd = sockfd;
  fds[1].events = POLLIN | POLLHUP;
  fds[1].revents = 0;

  char read_buf[BUFFER_SIZE];
  int pipefd[2];
  int ret = pipe(pipefd);
  assert(ret != -1);
  while (true) {
    ret = poll(fds, 2, -1);  // 2是fds的长度
    if (ret < 0) {
      printf("poll failed");
      break;
    }
    if (fds[1].revents & POLLHUP) {
      printf("server closed");
      break;
    } else if (fds[1].revents & POLLIN) {  // 电平触发，此处不需要循环
      memset(read_buf, '\0', BUFFER_SIZE);
      recv(fds[1].fd, read_buf, BUFFER_SIZE - 1, 0);
      printf("%s", read_buf);
    }
    if (fds[0].revents & POLLIN) {
      // splice 内核中移动数据
      // 电平触发，此处不需要循环
      ret = splice(fds[0].fd, NULL, pipefd[1], NULL, 32768,
                   SPLICE_F_MORE | SPLICE_F_MOVE);
      assert(ret != -1);
      ret = splice(pipefd[0], NULL, fds[1].fd, NULL, 32768,
                   SPLICE_F_MORE | SPLICE_F_MOVE);
      assert(ret != -1);
    }
  }
  close(sockfd);
  return 0;
}