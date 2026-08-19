int f(int x) {
  struct __local_type1 { int a; };
  __local_type1 s;
  s.a = x;
  union { int t; unsigned long a; } u;
  u.t = s.a;
  return (int)u.a;
}
int main() { return f(7) - 7; }
