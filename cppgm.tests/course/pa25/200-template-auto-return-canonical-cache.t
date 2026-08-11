template<class T>
constexpr auto value(T input)
{
  return input;
}

template<class T>
auto * pointer(T * input)
{
  return input;
}

template<class T>
auto & lvalue(T & input)
{
  return input;
}

template<class T>
auto && collapse(T & input)
{
  return input;
}

int main()
{
  int x = 1;
  lvalue(x) = 2;
  *pointer(&x) = 3;
  collapse(x) = 4;
  return value(x) + value(0) - 4;
}
