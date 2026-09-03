// Selecting a nondependent base specialization instantiates its definition, so a
// failing static_assert in its body is rejected.

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
