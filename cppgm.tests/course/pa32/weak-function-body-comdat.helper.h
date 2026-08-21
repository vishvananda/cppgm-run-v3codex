int comdat_leaf(int value);
extern int (*comdat_leaf_ptr)(int);

__attribute__((noinline)) inline int comdat_body_value(int value)
{
  value = value * 3 + 1;
  value = value * 5 + 2;
  value = value * 7 + 3;
  value = value * 11 + 4;
  return comdat_leaf_ptr(value);
}
