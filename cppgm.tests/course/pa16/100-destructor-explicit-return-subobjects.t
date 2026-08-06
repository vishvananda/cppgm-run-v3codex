// AUDIT: returning from a destructor still enters its subobject epilogue.

int g;

struct YA {
  ~YA() { g = 1; }
};

struct YB {
  YA member;
  ~YB() { return; }
};

int main() {
  YB value;
  return 0;
}
