using array_type = int[2];

int main()
{
  int prvalue_sum = 0;
  for (int value : array_type{1, 2})
    prvalue_sum = prvalue_sum + value;

  array_type values = {3, 4};
  int xvalue_sum = 0;
  for (int& value : static_cast<array_type&&>(values))
    xvalue_sum = xvalue_sum + value;

  return prvalue_sum == 3 && xvalue_sum == 7 ? 0 : 1;
}
