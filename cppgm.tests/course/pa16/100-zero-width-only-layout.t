struct EmptySeparator
{
  unsigned : 0;
};

int main()
{
  return sizeof(EmptySeparator) + alignof(EmptySeparator);
}
