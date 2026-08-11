// N3485 focus: 5.19 [expr.const] and 7.1.5 [dcl.constexpr].
// A non-constexpr conversion function cannot be invoked by a constant
// expression, even when its body happens to return a constant value.

struct flag
{
  static constexpr bool result = true;

  operator bool() const
  {
    return result;
  }
};

constexpr bool value = flag() ? true : false;

int main()
{
  return value ? 0 : 1;
}
