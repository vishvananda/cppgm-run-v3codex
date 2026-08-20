template<class T>
int run()
{
  auto first = []() -> int {
    static int value = 1;
    return value;
  };
  auto second = []() -> int {
    static int value = 2;
    return value;
  };
  return first() + second();
}

int main()
{
  return run<int>() + run<char>() == 6 ? 0 : 1;
}
