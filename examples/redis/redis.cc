#include <hiredis/hiredis.h>

#include <cstring>
#include <iostream>

int main() {
  redisContext* rctx = redisConnect("localhost", 6379);
  if (rctx == nullptr || rctx->err) {
    if (rctx) {
      printf("Error: %s\n", rctx->errstr);
      // handle error
    } else {
      printf("Can't allocate redis context\n");
    }
  }

  redisReply* reply =
      reinterpret_cast<redisReply*>(redisCommand(rctx, "set foo1 bar"));
  reply =
      reinterpret_cast<redisReply*>(redisCommand(rctx, "set foo2 %d", 1000));
  reply =
      reinterpret_cast<redisReply*>(redisCommand(rctx, "set foo3 %s", "hello"));
  reply = reinterpret_cast<redisReply*>(
      redisCommand(rctx, "set foo3 %b", "1234", (size_t)strlen("1234")));
  reply = reinterpret_cast<redisReply*>(
      redisCommand(rctx, "SET key:%s %s", "foo4", "bar4"));
  std::cout << reply->str << '\n';
  reply = reinterpret_cast<redisReply*>(
    redisCommand(rctx, "get key:%s", "foo4"));
    std::cout << reply->str <<'\n';
  redisFree(rctx);
}