template<class T>
auto recurse(T value)
{
  return recurse(value);
}

int main()
{
  return recurse(0);
}
