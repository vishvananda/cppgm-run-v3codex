// VALIDATION: a qualified member expression looks in the nominated base.

struct base
{
  int value;
};

struct derived : base
{
  int value();
};

int read(derived& object)
{
  return object.base::value;
}
