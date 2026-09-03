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
