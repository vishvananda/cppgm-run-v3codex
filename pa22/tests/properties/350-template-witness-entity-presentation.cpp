template<class T>
struct descriptor
{
  static const int value = 0;
};

template<class R, class... Arguments>
struct descriptor<R(Arguments...)>
{
  static const int value = 1;
};

template<class R, class... Arguments>
struct descriptor<R(Arguments...) const>
{
  static const int value = 2;
};

template<class R, class... Arguments>
struct descriptor<R(Arguments...) &>
{
  static const int value = 3;
};

static_assert(descriptor<void()>::value == 1, "");
static_assert(descriptor<void() const>::value == 2, "");
static_assert(descriptor<void() &>::value == 3, "");

int main()
{
  return descriptor<void()>::value +
      descriptor<void() const>::value +
      descriptor<void() &>::value == 6 ? 0 : 1;
}
