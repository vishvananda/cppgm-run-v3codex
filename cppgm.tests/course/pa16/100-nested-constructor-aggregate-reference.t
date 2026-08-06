// AUDIT: nested constructor aggregate actions store reference identity.

int g;
struct YLeaf { int& reference; };
struct YMid { YLeaf leaf; };
struct YOuter {
  YMid mid = {{g}};
  YOuter() {}
};

int main() {
  YOuter object;
  return &object.mid.leaf.reference == &g ? 0 : 1;
}
