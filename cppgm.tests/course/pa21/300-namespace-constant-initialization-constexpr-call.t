constexpr int seed()
{
  return 7;
}

int value = seed();

int main()
{
  return value == 7 ? 0 : 1;
}
