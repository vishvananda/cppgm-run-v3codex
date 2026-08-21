static int effect_count;

__attribute__((noinline)) static int effect()
{
  return ++effect_count;
}

__attribute__((noinline)) static int select_value(
    bool selected, int value, int)
{
  return selected ? value + 1 : value + 100;
}

int main()
{
  const int first = select_value(true, 20, effect());
  const int second = select_value(true, 30, 99);
  return first + second == 52 && effect_count == 1 ? 0 : 1;
}
