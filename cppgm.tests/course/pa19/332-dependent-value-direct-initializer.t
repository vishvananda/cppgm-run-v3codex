// A dependent qualified-id without typename is not a type, so a declarator
// whose parenthesized operand is such a name is a variable with a
// direct-initializer, not a function declaration needing typename.

struct policy
{
  enum kind { first, second };
  struct task
  {
    kind value;
    explicit task(kind v) : value(v) {}
  };
};
template<class Derived>
struct runner
{
  int run()
  {
    typename Derived::task after(Derived::second);
    return after.value == Derived::second ? 0 : 1;
  }
};
int main()
{
  runner<policy> r;
  return r.run();
}
