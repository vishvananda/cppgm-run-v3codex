template<class>
struct incomplete_base;

template<class T>
struct invalid_derived : incomplete_base<int>
{
};

int main()
{
  return 0;
}
