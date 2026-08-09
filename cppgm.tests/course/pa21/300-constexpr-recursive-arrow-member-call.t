// AUDIT: every arrow consumer uses the selected operator-> chain and carries
// its pointer/object facts into direct member-call overload resolution.
struct target {
  int value;
  constexpr int read() const { return value; }
};

struct inner_arrow {
  target object;
  constexpr target const *operator->() const { return &object; }
};

struct outer_arrow {
  inner_arrow object;
  constexpr inner_arrow const &operator->() const { return object; }
};

constexpr outer_arrow pointer = {{{17}}};
static_assert(pointer->read() == 17,
              "recursive operator-> must feed direct member calls");

constexpr int twice(int input) { return input * 2; }

struct surrogate {
  typedef int (*function)(int);
  constexpr operator function() const { return &twice; }
};

static_assert(surrogate()(9) == 18,
              "a constexpr callable surrogate retains its function address");

int main() { return 0; }
