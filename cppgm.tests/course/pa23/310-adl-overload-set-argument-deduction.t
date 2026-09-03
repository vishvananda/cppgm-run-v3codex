// An unqualified call found only by argument-dependent lookup must resolve an
// overload-set argument in a deduced position, as N3485 14.8.2.1/6 requires for
// an ordinary qualified call.  Only one member of each overload set matches the
// deduced function-pointer parameter, so trial deduction selects it.

namespace ns
{
  struct tag
  {
  };

  template<class T>
  T apply(tag, T (*f)(T), T seed)
  {
    return f(seed);
  }
}

int pick(int v)
{
  return v + 1;
}

int pick(int v, int w)
{
  return v + w;
}

long scale(long v)
{
  return v * 3;
}

long scale(long v, long w)
{
  return v * w;
}

int main()
{
  const int first = apply(ns::tag(), pick, 2);
  const long second = apply(ns::tag(), scale, 5L);
  return first == 3 && second == 15 ? 0 : 1;
}
