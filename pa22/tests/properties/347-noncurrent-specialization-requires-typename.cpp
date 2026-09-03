template<class T>
struct owner
{
  typedef int value_type;

  template<class U>
  static void invalid()
  {
    owner<U>::value_type value;
  }
};

int main()
{
  return 0;
}
