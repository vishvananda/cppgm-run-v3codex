union {
  int t;
  long a;
};

int f(int x) {
  t = x;
  return (int)a;
}
