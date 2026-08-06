// AUDIT: an unused class with an implicitly deleted destructor is well-formed.

struct YA {
  ~YA() = delete;
};

struct YB {
  YA member;
};

int main() {
  return 0;
}
