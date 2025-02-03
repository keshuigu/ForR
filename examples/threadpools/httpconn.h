#ifndef HTTPCONN_H_
#define HTTPCONN_H_

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace forr {
namespace http {

class HttpConn {
 public:
  HttpConn() {}
  ~HttpConn() {}

  void Init(int sockfd, const sockaddr_in& addr);
  void CloseConn(bool real_close = true);
  void Process();
  bool Read();
  bool Write();

  // 共享

  static int epollfd_;
  static int user_cnt_;

 private:
  static constexpr int kFileNameLen = 200;
  static constexpr int kReadBufferSize = 2048;
  static constexpr int kWriteBufferSize = 1024;

  enum Method {
    kGet = 0,
    kPost,
    kHead,
    kPut,
    kDelete,
    kTrace,
    kOptions,
    kConnect,
    kPatch,
  };

  enum CheckState {
    kReqLine = 0,
    kHeader,
    kContent,
  };

  enum HttpCode {
    kContinueReq,
    kGetReq,
    kBadReq,
    kNoRes,
    kForbiddenReq,
    kFileReq,
    kInternalError,
    kClosedConnection,
  };

  enum LineState {
    kLineOk = 0,
    kLineBad,
    kLineOpen,
  };

  void Init();
  HttpCode ProcessRead();
  bool ProcessWrite(HttpCode ret);

  HttpCode ParseReqLine(char* text);
  HttpCode ParseHeaders(char* text);
  HttpCode ParseContent(char* text);
  HttpCode DoReq();
  char* GetLine() { return read_buf_ + start_line_; }
  LineState ParseLine();

  void UnMap();
  bool AddResponse(const char* format, ...);
  bool AddContent(const char* content);
  bool AddStatusLine(int status, const char* title);
  bool AddHeaders(int content_length);
  bool AddContentLength(int content_length);
  bool AddLinger();
  bool AddBlankLine();

  int sockfd_;
  sockaddr_in address_;

  char read_buf_[kReadBufferSize];
  int read_idx_;    // 目前读取位置
  int check_idx_;   // 已解析位置
  int start_line_;  // 当前行起始

  char write_buf_[kWriteBufferSize];
  int write_idx_;

  CheckState check_state_;
  Method method_;

  char read_file_[kFileNameLen];
  char* url_;
  char* version_;
  char* host_;
  int content_length_;
  bool linger_;

  char* file_address_;

  struct stat file_stat_;

  // 拼接write，这样不用读取文件进用户态
  struct iovec iv_[2];

  int iv_cnt_;
};

inline int SetNonblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}

inline void AddFd(int epoll_fd, int fd, bool one_shot) {
  epoll_event e;
  e.data.fd = fd;
  e.events = EPOLLIN | EPOLLET;
  if (one_shot) {
    e.events |= EPOLLONESHOT;
  }

  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &e);
  SetNonblocking(fd);
}

inline void RemoveFd(int epoll_fd, int fd) {
  epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
  close(fd);
}

inline void ModFd(int epoll_fd, int fd, int ev) {
  epoll_event e;
  e.data.fd = fd;
  e.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
  epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &e);
}
}  // namespace http
}  // namespace forr

#endif  // HTTPCONN_H_