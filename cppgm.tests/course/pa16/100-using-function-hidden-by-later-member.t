// AUDIT: a member declared after a using-declaration hides the same base signature.

struct YUsingOrderBase {
  int select(int) { return 1; }
};

struct YUsingOrderDerived : YUsingOrderBase {
  using YUsingOrderBase::select;
  int select(int) { return 2; }
};

int main() {
  YUsingOrderDerived object;
  return object.select(0) == 2 ? 0 : 1;
}
