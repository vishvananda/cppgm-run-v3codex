// AUDIT: deleted constructors participate in overload resolution before rejection.

struct YDeletedSelected {
  YDeletedSelected(int) = delete;
  YDeletedSelected(double) {}
};

int main() {
  YDeletedSelected object(1);
  return 0;
}
