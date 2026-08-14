#include <initializer_list>

template<class T>
struct box {
  box();
  box(std::initializer_list<T>);
};

template<class T>
void consume(const box<T>&, T)
{
}

int main()
{
  consume({}, 1);
  consume({1, 2}, 1);
  return 0;
}
