#ifndef PROCESSPOOL_H_
#define PROCESSPOOL_H_

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
namespace forr {

namespace {

constexpr int kMaxProcessNumber = 16;
constexpr int kUserPerProcess = 65536;
constexpr int kMaxEventNumber = 10000;

int SetNonblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}

void AddFd(int epoll_fd, int fd) {
  epoll_event e;
  e.data.fd = fd;
  e.events = EPOLLIN | EPOLLET;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &e);
  SetNonblocking(fd);
}

void RemoveFd(int epoll_fd, int fd) {
  epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
  close(fd);
}

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

}  // namespace

class Process {
 public:
  Process() : pid_(-1) {};
  ~Process() {};

  pid_t pid_;
  int pipe_fd_[2];  // 父子进程通信管道
};

template <typename T>
class ProcessPool {
 public:
  // 单例模式
  static ProcessPool<T>* Create(int listen_fd, int process_number = 8) {
    if (instance_ == nullptr) {
      instance_ = new ProcessPool<T>(listen_fd, process_number);
    }
    return instance_;
  }

  static void Destory() {
    if (instance_ != nullptr) {
      delete instance_;
      instance_ = nullptr;
    }
  }

  void Run();

 private:
  ProcessPool(int listen_fd, int process_number = 8);
  ~ProcessPool() { delete[] sub_process_; }

  // 统一事件源
  // 信号处理函数的签名只能是 void handler(int)
  // 非静态成员函数存在隐式this指针参数
  static void SigHandler(int sig) {
    if (instance_ == nullptr) {
      return;
    }
    int old_errno = errno;
    int msg = sig;
    send(instance_->sig_pipefd_[1], (char*)&msg, 1, 0);
    errno = old_errno;
  }

  void SetupSigPipe();
  void RunParent();
  void RunChild();

  // 进程数
  int process_number_;
  // 子进程序号
  int idx_;
  // 当前进程的epollfd
  int epoll_fd_;
  // 监听socket (仅父进程)
  int listen_fd_;
  // 是否停止
  int stop_;
  // 统一事件源
  int sig_pipefd_[2];
  // 子进程信息数组
  Process* sub_process_;

  static ProcessPool<T>* instance_;
};

// 不同的T实际上对应不同的class

template <typename T>
ProcessPool<T>* ProcessPool<T>::instance_ = nullptr;

template <typename T>
ProcessPool<T>::ProcessPool(int listen_fd, int process_number)
    : listen_fd_(listen_fd),
      process_number_(process_number),
      idx_(-1),
      stop_(false) {
  assert((process_number_ > 0) && (process_number_ < kMaxProcessNumber));
  sub_process_ = new Process[process_number_];
  assert(sub_process_ != nullptr);

  for (int i = 0; i < process_number_; i++) {
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sub_process_[i].pipe_fd_);
    assert(ret == 0);
    sub_process_[i].pid_ = fork();
    // 子进程
    if (sub_process_[i].pid_ == 0) {
      // 全双工
      // 此处只要对应着关闭fd即可
      close(sub_process_[i].pipe_fd_[0]);
      idx_ = i;
      break;
    } else {
      close(sub_process_[i].pipe_fd_[1]);
      continue;
    }
  }
}

template <typename T>
void ProcessPool<T>::SetupSigPipe() {
  epoll_fd_ = epoll_create(5);  // 5为预估大小
  assert(epoll_fd_ != -1);

  int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd_);
  assert(ret != -1);

  SetNonblocking(sig_pipefd_[1]);
  AddFd(epoll_fd_, sig_pipefd_[0]);

  AddSig(SIGCHLD, SigHandler);
  AddSig(SIGTERM, SigHandler);
  AddSig(SIGINT, SigHandler);
  AddSig(SIGPIPE, SIG_IGN);
}

template <typename T>
void ProcessPool<T>::Run() {
  if (idx_ != -1) {
    RunChild();
  } else {
    RunParent();
  }
}

template <typename T>
void ProcessPool<T>::RunChild() {
  SetupSigPipe();

  int pipefd = sub_process_[idx_].pipe_fd_[1];
  AddFd(epoll_fd_, pipefd);
  epoll_event events[kMaxEventNumber];
  T* users = new T[kUserPerProcess];
  assert(users);
  int number = 0;
  int ret = -1;
  while (!stop_) {
    number = epoll_wait(epoll_fd_, events, kMaxEventNumber, -1);
    if (number < 0 && (errno != EAGAIN && errno != EINTR)) {
      std::printf("epoll failed, errno:%d\n", errno);
      perror("epoll child");
      break;
    }
    for (int i = 0; i < number; i++) {
      int sockfd = events[i].data.fd;
      if (sockfd == pipefd && (events[i].events & EPOLLIN)) {
        // 接收新连接
        int client = 0;
        ret = recv(sockfd, (char*)&client, sizeof(client), 0);
        // 读取失败 忽视
        if (ret <= 0) {
          continue;
        }
        struct sockaddr_in client_address;
        socklen_t client_length = sizeof(client_address);
        int connfd = accept(listen_fd_, (struct sockaddr*)&client_address,
                            &client_length);
        if (connfd < 0) {
          std::perror("accept");
          continue;
        }
        AddFd(epoll_fd_, connfd);
        // T 需要实现的方法
        users[connfd].Init(epoll_fd_, connfd, client_address);

      } else if (sockfd == sig_pipefd_[0] && (events[i].events & EPOLLIN)) {
        int sig;
        char signals[1024];
        ret = recv(sig_pipefd_[0], signals, sizeof(signals), 0);
        if (ret <= 0) {
          continue;
        }
        for (int j = 0; j < ret; j++) {
          switch (signals[j]) {
            case SIGCHLD: {
              pid_t pid;
              int stat;
              while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                continue;
              }
              break;
            }
            case SIGTERM:
            case SIGINT:
              stop_ = true;
              break;
            default:
              break;
          }
        }
      } else if (events[i].events & EPOLLIN) {
        // T 需要实现的方法
        users[sockfd].Process();
      }
      // 其他情况不处理
    }
  }
  // T 的析构函数中应该关闭相应的connfd
  delete[] users;
  users = nullptr;
  close(pipefd);
  // TODO 是否在此处关闭
  close(sig_pipefd_[0]);
  close(sig_pipefd_[1]);
  close(epoll_fd_);
  // listenfd 继承于父进程
}

template <typename T>
void ProcessPool<T>::RunParent() {
  SetupSigPipe();
  AddFd(epoll_fd_, listen_fd_);
  epoll_event events[kMaxEventNumber];
  int sub_process_cnt = 0;
  int new_conn = 1;
  int number = 0;
  int ret = -1;
  while (!stop_) {
    number = epoll_wait(epoll_fd_, events, kMaxEventNumber, -1);
    if (number < 0 && (errno != EAGAIN && errno != EINTR)) {
      std::printf("epoll failed, errno:%d\n", errno);
      perror("epoll parent");
      break;
    }
    for (int i = 0; i < number; i++) {
      int sockfd = events[i].data.fd;
      if (sockfd == listen_fd_ && (events[i].events & EPOLLIN)) {
        // Round Robin
        // sub_process_cnt 记录上次分配连接的子进程
        int j = (sub_process_cnt + 1) % process_number_;
        while (j != sub_process_cnt) {
          if (sub_process_[j].pid_ != -1) {
            break;
          }
          j = (j + 1) % process_number_;
        }
        // 所有子进程均结束
        if (sub_process_[j].pid_ == -1) {
          stop_ = true;
          break;
        }
        sub_process_cnt = j;
        send(sub_process_[j].pipe_fd_[0], (char*)&new_conn, sizeof(new_conn),
             0);
        std::printf("send request to child %d\n", j);
      } else if (sockfd == sig_pipefd_[0] && (events[i].events & EPOLLIN)) {
        int sig;
        char signals[1024];
        ret = recv(sig_pipefd_[0], signals, sizeof(signals), 0);
        if (ret <= 0) {
          continue;
        }
        for (int j = 0; j < ret; j++) {
          switch (signals[j]) {
            case SIGCHLD: {
              pid_t pid;
              int stat;
              while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                for (int j = 0; j < process_number_; j++) {
                  if (sub_process_[j].pid_ == pid) {
                    std::printf("child %d quit\n", j);
                    close(sub_process_[j].pipe_fd_[0]);
                    sub_process_[j].pid_ = -1;
                  }
                }
              }
              stop_ = true;
              for (int k = 0; k < process_number_; k++) {
                if (sub_process_[k].pid_ != -1) {
                  stop_ = false;
                  break;
                }
              }

              break;
            }
            case SIGTERM:
            case SIGINT:
              // stop_ = true;
              std::printf("kill all child now\n");
              for (int k = 0; k < process_number_; k++) {
                int pid = sub_process_[k].pid_;
                if (pid != -1) {
                  kill(pid, SIGTERM);
                }
              }
              break;
            default:
              break;
          }
        }
      }
    }
  }
  close(epoll_fd_);
  close(sig_pipefd_[0]);
  close(sig_pipefd_[1]);
}

}  // namespace forr
#endif