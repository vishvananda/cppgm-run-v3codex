// A qualified base name spelling a template parameter is not dependent and must be complete.

namespace external
{
struct T;
}

template<class T>
struct invalid_derived : external::T
{
};

int main()
{
  return 0;
}
