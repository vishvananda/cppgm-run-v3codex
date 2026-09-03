template<int Value>
struct number
{
  static const int value = Value;
};

struct any_argument
{
  any_argument(...);
};

int classify(any_argument);

template<class T>
struct observed
{
  static T &unevaluated_reference;
  static const int runtime_value = 8;
  static const int argument_value = 1;
  static const int unused_value = 3;

  enum {
    classified_size = sizeof(classify(unevaluated_reference))
  };

  static int run(int input)
  {
    return input < runtime_value ? number<argument_value>::value : 0;
  }
};

int main()
{
  return observed<int>::run(0) == 1 &&
    observed<int>::classified_size == sizeof(int) ? 0 : 1;
}
