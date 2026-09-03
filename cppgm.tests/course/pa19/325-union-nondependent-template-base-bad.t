// An unused class template deriving from a union is rejected.

union variant_base
{
  int value;
};

template<class T>
struct invalid_derived : variant_base
{
};

int main()
{
  return 0;
}
