volatile unsigned observed;

__attribute__((noinline)) unsigned loop_sum(unsigned count)
{
  unsigned total = 0;
  for(unsigned index = 0; index < count; ++index) {
    observed = index;
    total += observed;
  }
  return total;
}

int main()
{
  return loop_sum(4) != 6;
}
