int main()
{
  const unsigned sentinel = 37;
  struct Record
  {
    unsigned value = sentinel;
  };
  Record record;
  return record.value == 37 ? 0 : 1;
}
