// N3485 focus: 12.6.2 [class.base.init] Complete and base-object lifecycle
// entries retain their ABI identity independently of optional inlining policy.
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
