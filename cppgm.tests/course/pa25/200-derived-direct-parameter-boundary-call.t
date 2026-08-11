struct Base
{
  int value;
};

struct Direct : Base
{
  Direct(int initial) { value = initial; }
  Direct(const Direct & other) : Base(other) {}
};

int take_direct(Direct direct)
{
  return direct.value;
}

int main()
{
  Direct direct(9);
  return take_direct(direct) == 9 ? 0 : 1;
}
