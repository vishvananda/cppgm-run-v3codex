// AUDIT: a public static function inherited through a private base is private.

struct YStaticBase {
  static int value() { return 0; }
};

class YStaticDerived : YStaticBase {};

int main() {
  return YStaticDerived::value();
}
