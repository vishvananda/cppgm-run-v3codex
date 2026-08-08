// AUDIT: a base member introduced by a using-declaration is ranked at the
// using owner even when the call object is a further-derived class.
struct Base {
  int pick(int value) { return value + 1; }
};

struct Derived : Base {
  using Base::pick;
  int pick(double value) { return (int)value + 2; }
};

struct Further : Derived {};

int main() {
  Further value;
  return value.pick(4) == 5 ? 0 : 1;
}
