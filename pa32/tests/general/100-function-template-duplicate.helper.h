template<class T>
__attribute__((noinline)) int add_template(T x)
{
  return static_cast<int>(x) + 1;
}
