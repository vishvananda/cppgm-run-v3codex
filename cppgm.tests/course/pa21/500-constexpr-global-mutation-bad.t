int runtime_state = 0;

constexpr int mutate_runtime_state() {
  runtime_state = 3;
  return runtime_state;
}

static_assert(mutate_runtime_state() == 3,
  "constant evaluation may not mutate namespace storage");

int main() { return 0; }
