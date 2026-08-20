static union
{
  int t;
  long a;
};

int f(int x)
{
  t = x;
  return static_cast<int>(a);
}
