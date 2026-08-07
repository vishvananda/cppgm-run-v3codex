// AUDIT: a namespace function conflicts with an imported matching signature.

namespace YUsingSource {
int select(int) { return 1; }
}

namespace YUsingTarget {
using YUsingSource::select;
int select(int) { return 2; }
}

int main() {
  return 0;
}
