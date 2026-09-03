// An unevaluated member expression must not instantiate the member's retained
// out-of-class definition or its initializer dependency.

struct any_argument
{
  any_argument(...);
};

int select_result(any_argument);

template<class T>
T &make_value();

template<class T>
struct holder
{
  static T &value;

  enum {
    selected_size = sizeof(select_result(value))
  };
};

template<class T>
T &holder<T>::value = make_value<T>();

int main()
{
  return holder<int>::selected_size == sizeof(int) ? 0 : 1;
}
