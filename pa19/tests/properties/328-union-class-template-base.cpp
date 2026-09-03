template<class>
union variant_base
{
  int value;
};

template<class T>
struct invalid_derived : variant_base<int>
{
};

int main()
{
  return 0;
}
