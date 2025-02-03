#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include <pthread.h>
#include <semaphore.h>

#include <cassert>
#include <cstdio>
#include <list>
#include <mutex>

namespace forr {

// 封装信号量
class Sem {
 public:
  Sem() { assert(sem_init(&sem_, 0, 0) == 0); }
  ~Sem() { sem_destroy(&sem_); }

  bool Wait() { return sem_wait(&sem_) == 0; }

  bool Post() { return sem_post(&sem_) == 0; }

 private:
  sem_t sem_;
};

template <typename T>
class ThreadPool {
 public:
  ThreadPool(int thread_number = 8, int max_requests = 10000);
  ~ThreadPool();

  bool Append(T* req);

 private:
  static void* Worker(void* arg);
  void Run();

  int thread_number_;
  int max_requests_;
  pthread_t* threads_;
  // list 是链表
  std::list<T*> work_queue_;
  // 互斥锁
  std::mutex mutex_;
  Sem sem_;
  bool stop_;
};

template <typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests)
    : thread_number_(thread_number), max_requests_(max_requests) {
  assert(thread_number_ > 0 && max_requests_ > 0);
  threads_ = new pthread_t[thread_number_];
  assert(threads_ != nullptr);
  for (int i = 0; i < thread_number_; i++) {
    std::printf("create thread %d \n", i);
    // 此处应有错误处理
    assert(pthread_create(&threads_[i], nullptr, Worker, this) == 0);
    assert(pthread_detach(threads_[i]) == 0);
  }
}

template <typename T>
ThreadPool<T>::~ThreadPool() {
  delete[] threads_;
}

template <typename T>
bool ThreadPool<T>::Append(T* request) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (work_queue_.size() > max_requests_) {
    return false;
  }
  work_queue_.push_back(request);
  // 通知其他线程
  sem_.Post();
  return true;
}

template <typename T>
void* ThreadPool<T>::Worker(void* arg) {
  ThreadPool* pool = reinterpret_cast<ThreadPool*>(arg);
  pool->Run();
  return pool;
}

template <typename T>
void ThreadPool<T>::Run() {
  while (!stop_) {
    sem_.Wait();
    T* req = nullptr;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (work_queue_.empty()) {
        continue;
      }
      req = work_queue_.front();
      work_queue_.pop_front();
    }
    if (req) {
      req->Process();
    }
  }
}

}  // namespace forr
#endif