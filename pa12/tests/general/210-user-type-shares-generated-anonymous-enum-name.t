// A user type spelled like a generated anonymous-enum identity coexists
// with an anonymous enum; the generated identity is not in lookup.
struct __anonymous_enum1 { int a; };
enum { K1, K2 };
int main() {
  __anonymous_enum1 s;
  s.a = K2;
  return s.a - 1;
}
