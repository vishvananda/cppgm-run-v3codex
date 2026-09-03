template<class T>
struct invalid_constant
{
  static constexpr int value = sizeof(typename T::missing);
  constexpr operator int() const { return value; }
};

struct argument
{
};

static_assert(invalid_constant<argument>{}, "must instantiate value");

int main()
{
  return 0;
}
