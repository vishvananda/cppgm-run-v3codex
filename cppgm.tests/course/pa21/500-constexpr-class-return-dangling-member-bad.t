struct pointer_value {
  const int *pointer;
};

struct wrapper {
  pointer_value nested;
};

constexpr wrapper dangling() {
  int value = 7;
  return wrapper{pointer_value{&value}};
}

static_assert(dangling().nested.pointer != 0,
  "a returned object may not retain invocation-local storage");

int main() { return 0; }
