bool less_than(int left, int right)
{
  bool result = left < right;
  return result;
}

int main()
{
  return less_than(1, 2) ? 0 : 1;
}
