struct holder
{
  int value;

  int read()
  {
    auto get = []() { return value; };
    return get();
  }
};

int main()
{
  holder object;
  return object.read();
}
