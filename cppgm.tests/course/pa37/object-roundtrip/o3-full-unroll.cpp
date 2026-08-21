volatile int observed;

int main()
{
  int sum = 0;
  for (int i = 0; i < 4; ++i) {
    sum += i;
    observed = sum;
  }
  return sum == 6 && observed == 6 ? 0 : 1;
}
