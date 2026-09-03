// A member template's qualified call through a using-introduced base template
// must resolve against its own class-template specialization, even when several
// specializations replay the same call and are instantiated in a different order.

template<class A>
struct traits
{
  template<class T>
  static int construct(A&, T* p) { return *p; }
};

template<class A>
struct alloc_traits : traits<A>
{
  typedef traits<A> base_type;
  using base_type::construct;
};

template<class T>
struct holder
{
  typedef alloc_traits<T> tr;
  struct temp
  {
    template<class U>
    temp(holder* h, U) { T a = T(); result = tr::construct(a, h->value); }
    int result;
  };
  T* value;
  int run() { temp t(this, 0); return t.result; }
};

int main()
{
  holder<long> first;
  long l = 5;
  first.value = &l;
  holder<int> second;
  int x = 7;
  second.value = &x;
  return first.run() + second.run() == 12 ? 0 : 1;
}
