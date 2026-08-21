template<class T>
struct Hash
{
};

template<class T>
struct Eq
{
};

template<class T>
struct Alloc
{
};

template<class K, class H, class E, class A>
struct Table
{
  __attribute__((noinline)) Table() {}
  ~Table() {}

  __attribute__((noinline)) int value(int x)
  {
    return x + 1;
  }
};

using MyTable = Table<int, Hash<int>, Eq<int>, Alloc<int> >;
