// AUDIT: copy-list initialization rejects a selected explicit constructor.

struct YCopyList {
  explicit YCopyList(int) {}
  YCopyList(double) {}
};

int main() {
  YCopyList object = {1};
  return 0;
}
