static bool select_comparison(bool first, int left, int right)
{
  if (first)
    return left < right;
  return left >= right;
}

int main()
{
  return select_comparison(true, 1, 2) &&
      select_comparison(false, 2, 1) ? 0 : 1;
}
