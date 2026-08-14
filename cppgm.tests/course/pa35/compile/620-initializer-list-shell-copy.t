#include <initializer_list>

template<class T>
const std::initializer_list<T>& retain(const std::initializer_list<T>& values)
{
  return values;
}

int first(std::initializer_list<int> values)
{
  return *values.begin();
}

int main()
{
  return first(retain({0, 1}));
}
