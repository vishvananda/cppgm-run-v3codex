struct one
{
  char value;
};

struct sixteen
{
  long first;
  long second;
};

__attribute__((always_inline)) inline one pass_one(one value)
{
  return value;
}

__attribute__((always_inline)) inline sixteen pass_sixteen(sixteen value)
{
  return value;
}

one use_one(one value)
{
  return pass_one(value);
}

sixteen use_sixteen(sixteen value)
{
  return pass_sixteen(value);
}

int use()
{
  one small = {'q'};
  sixteen large = {17, 29};
  one small_copy = use_one(small);
  sixteen large_copy = use_sixteen(large);
  return small_copy.value == 'q' &&
         large_copy.first == 17 && large_copy.second == 29 ? 0 : 1;
}
