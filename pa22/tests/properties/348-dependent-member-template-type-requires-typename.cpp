template<class T>
struct owner
{
  typedef T::template rebind<int> invalid_type;
};

int main()
{
  return 0;
}
