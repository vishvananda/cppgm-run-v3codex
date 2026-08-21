volatile int observed;

int main()
{
  for (int i = 0; i < 3; ++i)
    observed = i;
  return observed == 2 ? 0 : 1;
}
