struct bulk_value
{
  long words[10];
};

struct bulk_owner
{
  bulk_value value;

  bulk_owner() : value() {}
};

int main()
{
  bulk_owner object;
  for (int i = 0; i != 10; ++i)
    if (object.value.words[i] != 0)
      return 1;
  return 0;
}
