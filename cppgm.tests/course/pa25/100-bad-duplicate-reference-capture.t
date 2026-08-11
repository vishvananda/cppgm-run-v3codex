int main()
{
  int value = 1;
  auto read = [&value, &value]() { return value; };
  return read();
}
