int calls;
int values[3] = {1, 2, 3};

int (&range())[3]
{
  ++calls;
  return values;
}

int main()
{
  int sum = 0;
  for (int value : range())
    sum = sum + value;
  return calls == 1 && sum == 6 ? 0 : 1;
}
