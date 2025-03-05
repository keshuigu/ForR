#include <iostream>

class A {
 public:
  int a;
  A() : a(1) {}

  A(int x):A(){
    a = x;
    // this->A();
  }
};

int main() { std::cout << A().a << "\n";
  std::cout << A(2).a << "\n"; }