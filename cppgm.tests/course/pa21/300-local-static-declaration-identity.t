template<int N>
int pick(bool second)
{
  if (second)
  {
    static int value = N + 1;
    return value;
  }
  static int value = N;
  return value;
}

int main()
{
  return pick<3>(false) == 3 && pick<3>(true) == 4 ? 0 : 1;
}
