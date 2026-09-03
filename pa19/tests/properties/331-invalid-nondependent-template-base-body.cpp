template<bool Valid>
struct checked_base
{
  static_assert(Valid, "selected base specialization must be valid");
};

template<class T>
struct derived : checked_base<false>
{
};

int main()
{
  return 0;
}
