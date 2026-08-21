template<class T>
struct Box {
  __attribute__((noinline)) int twice(int x)
  {
    return x * 2;
  }
};
