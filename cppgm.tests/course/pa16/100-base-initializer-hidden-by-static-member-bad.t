// AUDIT: class-scope member lookup hides a same-named direct base initializer.

struct YInitializerBase {};

struct YInitializerDerived : YInitializerBase {
  static int YInitializerBase;
  YInitializerDerived() : YInitializerBase() {}
};

int main() {
  YInitializerDerived value;
  return 0;
}
