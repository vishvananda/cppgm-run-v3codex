struct member_owner
{
  int T;
};

extern member_owner object;

template<class T>
struct invalid_derived : decltype(object.T)
{
};

int main()
{
  return 0;
}
