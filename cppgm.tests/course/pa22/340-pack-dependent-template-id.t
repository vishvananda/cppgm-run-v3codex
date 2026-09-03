// A template-id whose argument is sizeof... over a pack stays dependent while a fixed template-id does not.

template<unsigned long Value>
struct selected
{
  static const unsigned long value = Value;
};

template<class... Types>
struct observed
{
  typedef selected<2> fixed;
  typedef selected<sizeof...(Types)> dependent;

  static unsigned long run()
  {
    return fixed::value + dependent::value;
  }
};

int main()
{
  return observed<int, char>::run() == 4 ? 0 : 1;
}
