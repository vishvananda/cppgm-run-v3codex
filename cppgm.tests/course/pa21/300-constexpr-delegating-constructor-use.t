// AUDIT: delegating completion reuses the target constructor's immutable
// complete-object fact before evaluating the delegating constructor body.
struct value {
  int member;
  constexpr value() : member(7) {}
  constexpr value(int) : value() {}
};

static_assert(value(0).member == 7, "delegated object value");

int main() { return 0; }
