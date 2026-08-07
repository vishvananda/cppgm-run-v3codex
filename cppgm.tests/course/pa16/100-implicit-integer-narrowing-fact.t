unsigned char value;

int sink(unsigned char input)
{
  return input;
}

int main()
{
  value = 258;
  unsigned char local = 259;
  return sink(260) + value + local;
}
