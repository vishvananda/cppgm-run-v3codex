// A member type of a different specialization of the current template requires typename.

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
