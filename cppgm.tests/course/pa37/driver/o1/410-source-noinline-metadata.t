__attribute__((noinline)) int retained_leaf(int value)
{
  return value + 1;
}

int main()
{
  return retained_leaf(4) - 5;
}
