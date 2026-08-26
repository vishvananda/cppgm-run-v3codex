__attribute__((noinline)) long helper(long value)
{
  return value + 1;
}

int main()
{
  return helper(41) != 42;
}
