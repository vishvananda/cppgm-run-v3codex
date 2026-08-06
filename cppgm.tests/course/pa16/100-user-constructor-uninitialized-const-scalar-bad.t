// AUDIT: a user-provided constructor must initialize each const scalar member.

struct YConst {
  const int value;
  YConst() {}
};

int main() {
  YConst object;
  return 0;
}
