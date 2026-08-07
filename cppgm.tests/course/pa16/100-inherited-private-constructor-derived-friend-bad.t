class Base
{
  Base(int) {}
};

class Derived : Base
{
  using Base::Base;
  friend int make();
};

int make()
{
  Derived value(1);
  return 0;
}

int main()
{
  return make();
}
