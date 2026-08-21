struct Poly {
  __attribute__((noinline)) Poly() {}
  virtual int f() const { return 7; }
  __attribute__((noinline)) virtual ~Poly() {}
};
