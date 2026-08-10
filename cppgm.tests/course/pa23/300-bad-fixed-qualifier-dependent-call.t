// N3485 focus: 14.6.2 [temp.dep] and 14.6.3 [temp.nondep].
// Dependent template arguments do not defer lookup in a fixed qualifier.

namespace fixed {}

template<class T>
auto skipped(T) -> decltype(fixed::missing<T>());

int main()
{
  return 0;
}
