extern "C" long power_two_difference(long *first, long *last)
{
  return last - first;
}

extern "C" long general_difference(char (*first)[3], char (*last)[3])
{
  return last - first;
}
