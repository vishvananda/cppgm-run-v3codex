struct VirtualBase
{
  int value;

  VirtualBase() noexcept : value(7) {}
  ~VirtualBase() noexcept {}
};

struct Derived : virtual VirtualBase
{
  Derived() noexcept {}
  ~Derived() noexcept {}
};

int use_derived()
{
  Derived value;
  return value.value;
}
