template<int Value>
struct number
{
  static const int value = Value;
};

template<class T>
struct observed
{
  static const int runtime_value = 8;
  static const int argument_value = 1;
  static const int unused_value = 3;

  static int run(int input)
  {
    return input < runtime_value ? number<argument_value>::value : 0;
  }
};

int main()
{
  return observed<int>::run(0) == 1 ? 0 : 1;
}
