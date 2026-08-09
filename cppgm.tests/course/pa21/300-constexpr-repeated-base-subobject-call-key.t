// AUDIT: a completed member call owns both the immutable complete-object fact
// and the selected active-base fact; repeated base types cannot alias in cache.
struct shared_base {
  int value;
  constexpr shared_base(int input) : value(input) {}
  constexpr int read() const { return value; }
};

struct left_base : shared_base {
  constexpr left_base() : shared_base(1) {}
};

struct right_base : shared_base {
  constexpr right_base() : shared_base(2) {}
};

struct complete : left_base, right_base {
  constexpr complete() : left_base(), right_base() {}
};

static_assert(static_cast<left_base const &>(complete()).read() == 1,
              "the left receiver base path owns member projection");
static_assert(static_cast<right_base const &>(complete()).read() == 2,
              "the right receiver path is part of the completed-call key");

int main() { return 0; }
