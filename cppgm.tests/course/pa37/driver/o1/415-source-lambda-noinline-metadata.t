int main()
{
  auto retained = [](int value) __attribute__((noinline)) {
    return value + 1;
  };
  return retained(4) - 5;
}
