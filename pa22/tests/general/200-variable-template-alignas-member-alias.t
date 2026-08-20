namespace stdish
{
  inline namespace v1
  {
    template<class T>
    const unsigned long alignment = __alignof(T);
  }
}

template<class T>
struct holder
{
  typedef T value_type;

  struct
  {
    alignas(::stdish::alignment<value_type>) char data;
  };
};

holder<long long> object;

int main()
{
  return alignof(holder<long long>) == alignof(long long) ? 0 : 1;
}
