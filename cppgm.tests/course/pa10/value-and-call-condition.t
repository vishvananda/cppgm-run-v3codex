bool check(int);

int test(bool success)
{
  if (success && check(0)) return 1;
  return 0;
}
