#include "httpconn.h"

#include <sys/uio.h>

#include <cstring>
#include <string>
namespace forr {
namespace http {
namespace {
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form =
    "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form =
    "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form =
    "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form =
    "There was an unusual problem serving the requested file.\n";
// TODO
const char* doc_root = "/var/www/html";

}  // namespace

int HttpConn::user_cnt_ = 0;
int HttpConn::epollfd_ = -1;

void HttpConn::CloseConn(bool real_close) {
  if (real_close && sockfd_ != -1) {
    RemoveFd(epollfd_, sockfd_);
    sockfd_ = -1;
    user_cnt_--;
  }
}

void HttpConn::Init(int sockfd, const sockaddr_in& addr) {
  sockfd_ = sockfd;
  address_ = addr;
  // for test
  int reuse = 1;
  setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  AddFd(epollfd_, sockfd_, true);
  user_cnt_++;
  Init();
}

void HttpConn::Init() {
  check_state_ = kReqLine;
  linger_ = false;
  method_ = kGet;
  url_ = nullptr;
  version_ = nullptr;
  content_length_ = 0;
  host_ = nullptr;
  start_line_ = 0;
  check_idx_ = 0;
  read_idx_ = 0;
  write_idx_ = 0;
  memset(read_buf_, '\0', kReadBufferSize);
  memset(write_buf_, '\0', kWriteBufferSize);
  memset(read_file_, '\0', kFileNameLen);
}

bool HttpConn::Read() {
  if (read_idx_ >= kReadBufferSize) {
    return false;
  }
  int bytes_read = 0;
  while (true) {
    bytes_read =
        recv(sockfd_, read_buf_ + read_idx_, kReadBufferSize - read_idx_, 0);
    if (bytes_read == -1) {
      // 无数据可读
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      return false;
    }
    // 链接关闭
    else if (bytes_read == 0) {
      return false;
    } else {
      read_idx_ += bytes_read;
    }
  }
  return true;
}

HttpConn::LineState HttpConn::ParseLine() {
  char t;
  for (; check_idx_ < read_idx_; check_idx_++) {
    t = read_buf_[check_idx_];
    // 找'\n
    if (t == '\r') {
      if (check_idx_ + 1 == read_idx_) {
        return kLineOpen;
      } else if (read_buf_[check_idx_ + 1] == '\n') {
        // 将该两位置0
        read_buf_[check_idx_++] = '\0';
        read_buf_[check_idx_++] = '\0';
        return kLineOk;
      } else {
        return kLineBad;
      }
    }
    // 向前找'\r'
    else if (t == '\n') {
      if (check_idx_ > 1 && read_buf_[check_idx_ - 1] == '\r') {
        read_buf_[check_idx_ - 1] = '\0';
        read_buf_[check_idx_++] = '\0';
        return kLineOk;
      } else {
        return kLineBad;
      }
    }
  }
  return kLineOpen;
}

HttpConn::HttpCode HttpConn::ParseReqLine(char* text) {
  // 找第一个空格或者\t
  url_ = std::strpbrk(text, " \t");
  if (url_ == nullptr) {
    return kBadReq;
  }
  *url_ = '\0';
  url_++;
  // url 前被置为0
  // 因此原字符串按c风格字符串解析后被截断
  // strcasecmp 直接比较
  // char* method = text;
  // if (strcasecmp(method, "GET") == 0) {
  //   method_ = kGet;
  // }
  std::string m(text);
  if (m == "GET") {
    method_ = kGet;
  } else {
    return kBadReq;
  }

  url_ += std::strspn(url_, " \t");      // 移除可能存在的" \t"
  version_ = std::strpbrk(url_, " \t");  // 找到url后下一个" \t"
  *version_ = '\0';
  version_++;
  version_ += std::strspn(version_, " \t");  // 同样处理

  std::string v(version_);
  if (v != "HTTP/1.1") {
    return kBadReq;
  }
  // 忽略域名
  std::string u(url_);
  assert(u.size() >= 7);
  if (u.substr(0, 7) == "http://") {
    url_ += 7;
    url_ = std::strchr(url_, '/');
  }
  if (url_ == nullptr || url_[0] != '/') {
    return kBadReq;
  }
  check_state_ = kHeader;
  return kContinueReq;
}
HttpConn::HttpCode HttpConn::ParseHeaders(char* text) {
  // 空行代表解析完毕
  if (text[0] == '\0') {
    if (content_length_ != 0) {
      check_state_ = kContent;
      return kContinueReq;
    }
    // 所有请求内容解析完毕
    return kGetReq;
  }
  std::string t(text);
  size_t p = t.find(':');
  assert(p != std::string::npos);
  std::string header = t.substr(0, p);
  if (header == "Connection") {
    text += 11;
    text += std::strspn(text, " \t");
    std::string c(text);
    if (c == "keep-alive") {
      linger_ = true;
    }
  } else if (header == "Content-Length") {
    text += 15;
    text += std::strspn(text, " \t");
    content_length_ = std::atoi(text);
  } else if (header == "Host") {
    text += 5;
    text += std::strspn(text, " \t");
    host_ = text;
  } else {
    std::printf("unknow header %s\n", text);
  }
  return kContinueReq;
}
HttpConn::HttpCode HttpConn::ParseContent(char* text) {
  // 事实上不做解析
  if (read_idx_ >= content_length_ + check_idx_) {
    text[content_length_] = '\0';
    return kGetReq;
  }
  return kContinueReq;
}

HttpConn::HttpCode HttpConn::ProcessRead() {
  LineState line_state = kLineOk;
  HttpCode ret = kContinueReq;
  char* text = nullptr;
  while ((check_state_ == kContent && line_state == kLineOk) ||
         ((line_state = ParseLine()) == kLineOk)) {
    text = GetLine();
    start_line_ = check_idx_;
    std::printf("got one line:%s\n", text);
    switch (check_state_) {
      case kReqLine:
        ret = ParseReqLine(text);
        if (ret == kBadReq) {
          return kBadReq;
        }
        break;
      case kHeader:
        ret = ParseHeaders(text);
        if (ret == kBadReq) {
          return kBadReq;
        } else if (ret == kGetReq) {
          return DoReq();
        }
        break;
      case kContent:
        ret = ParseContent(text);
        if (ret == kGetReq) {
          return DoReq();
        }
        line_state = kLineOpen;  // content 未读完
        break;

      default:
        return kInternalError;
    }
  }
  return kContinueReq;
}

HttpConn::HttpCode HttpConn::DoReq() {
  std::strcpy(read_file_, doc_root);
  int len = strlen(doc_root);
  std::strncpy(read_file_ + len, url_, kFileNameLen - len - 1);
  if (stat(read_file_, &file_stat_) < 0) {
    return kNoRes;
  }
  if (!(file_stat_.st_mode & S_IROTH)) {
    return kForbiddenReq;
  }
  if (S_ISDIR(file_stat_.st_mode)) {
    return kBadReq;
  }
  // 是否可以不读取文件
  int fd = open(read_file_, O_RDONLY);
  file_address_ =
      (char*)mmap(0, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  return kFileReq;
}

void HttpConn::UnMap() {
  if (file_address_) {
    munmap(file_address_, file_stat_.st_size);
    file_address_ = nullptr;
  }
}

bool HttpConn::Write() {
  if (write_idx_ == 0) {
    ModFd(epollfd_, sockfd_, EPOLLIN);
    Init();
    return true;
  }
  size_t bytes = 0;
  for (int i = 0; i < iv_cnt_; i++) {
    bytes += iv_[i].iov_len;
  }
  while (bytes > 0) {
    int bytes_written = 0;
    bytes_written = writev(sockfd_, iv_, iv_cnt_);
    if (bytes_written <= 0) {
      // 等待下一次写
      if (errno == EAGAIN) {
        ModFd(epollfd_, sockfd_, EPOLLOUT);
        return true;
      }
      // 出错 释放资源
      UnMap();
      return false;
    }
    bytes -= bytes_written;
    for (int i = 0; i < iv_cnt_; ++i) {
      if (bytes_written >= iv_[i].iov_len) {
        bytes_written -= iv_[i].iov_len;
        iv_[i].iov_len = 0;
      } else {
        iv_[i].iov_base = (char*)iv_[i].iov_base + bytes_written;
        iv_[i].iov_len -= bytes_written;
        break;
      }
    }
  }
  UnMap();
  if (linger_) {
    Init();
    ModFd(epollfd_, sockfd_, EPOLLIN);
    return true;
  } else {
    ModFd(epollfd_, sockfd_, EPOLLIN);
    return false;
  }
}

bool HttpConn::AddResponse(const char* format, ...) {
  if (write_idx_ >= kWriteBufferSize) {
    return false;
  }
  va_list v;
  va_start(v, format);
  int len = std::vsnprintf(write_buf_ + write_idx_,
                           kWriteBufferSize - 1 - write_idx_, format, v);
  va_end(v);
  if (len >= kWriteBufferSize - 1 - write_idx_) {
    return false;
  }
  write_idx_ += len;
  return true;
}

bool HttpConn::AddStatusLine(int status, const char* title) {
  return AddResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConn::AddHeaders(int content_len) {
  return AddContentLength(content_len) && AddLinger() && AddBlankLine();
}
bool HttpConn::AddContentLength(int content_len) {
  return AddResponse("Content-Length: %d\r\n", content_len);
}

bool HttpConn::AddLinger() {
  return AddResponse("Connection: %s\r\n", (linger_) ? "keep-alive" : "close");
}

bool HttpConn::AddBlankLine() { return AddResponse("\r\n"); }

bool HttpConn::AddContent(const char* content) {
  return AddResponse("%s", content);
}

bool HttpConn::ProcessWrite(HttpCode ret) {
  switch (ret) {
    case kInternalError:
      AddStatusLine(500, error_500_title);
      AddHeaders(strlen(error_500_form));
      if (!AddContent(error_500_form)) {
        return false;
      }
      break;

    case kBadReq:
      AddStatusLine(400, error_400_title);
      AddHeaders(strlen(error_400_form));
      if (!AddContent(error_400_form)) {
        return false;
      }
      break;
    case kNoRes:
      AddStatusLine(404, error_404_title);
      AddHeaders(strlen(error_404_form));
      if (!AddContent(error_404_form)) {
        return false;
      }
      break;

    case kForbiddenReq:
      AddStatusLine(403, error_403_title);
      AddHeaders(strlen(error_403_form));
      if (!AddContent(error_403_form)) {
        return false;
      }
      break;

    case kFileReq:
      AddStatusLine(200, ok_200_title);
      if (file_stat_.st_size != 0) {
        AddHeaders(file_stat_.st_size);
        iv_[0].iov_base = write_buf_;
        iv_[0].iov_len = write_idx_;
        iv_[1].iov_base = file_address_;
        iv_[1].iov_len = file_stat_.st_size;
        iv_cnt_ = 2;
        return true;
      } else {
        const char* ok_string = "<html><body></body></html>";
        AddHeaders(strlen(ok_string));
        if (!AddContent(ok_string)) {
          return false;
        }
      }
      break;
    default:
      return false;
  }
  iv_[0].iov_base = write_buf_;
  iv_[0].iov_len = write_idx_;
  iv_cnt_ = 1;
  return true;
}

void HttpConn::Process() {
  HttpCode ret = ProcessRead();
  if (ret == kContinueReq) {
    ModFd(epollfd_, sockfd_, EPOLLIN);
    return;
  }
  bool write_ret = ProcessWrite(ret);
  if (!write_ret) {
    CloseConn();
  }
  ModFd(epollfd_, sockfd_, EPOLLOUT);
}
}  // namespace http

}  // namespace forr
