struct Base
{
  int value;
  Base(int input) : value(input) {}
};

struct Derived : Base
{
  Derived(int input) : Base(7) {}
  using Base::Base;
};

int main()
{
  Derived value(3);
  return value.value;
}
