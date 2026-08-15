int finite(long double value)
{
  return __builtin_isfinite(value);
}

int infinite(long double value)
{
  return __builtin_isinf(value);
}

int normal(long double value)
{
  return __builtin_isnormal(value);
}

int classify(long double value)
{
  return __builtin_fpclassify(1, 2, 3, 4, 5, value);
}

int main()
{
  return 0;
}
