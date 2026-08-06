// AUDIT: a union destructor does not automatically destroy variant members.

int g;

struct YA {
  ~YA() { g = 1; }
};

union YU {
  YA member;
  int selected;
  YU() : selected(0) {}
  ~YU() {}
};

int main() {
  YU value;
  return 0;
}
