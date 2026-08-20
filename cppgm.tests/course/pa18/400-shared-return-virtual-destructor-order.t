struct Base
{
  virtual ~Base() {}
};

int choose(bool first)
{
  Base value;
  if(first) return 7;
  return 9;
}

int main()
{
  return choose(true) != 7 || choose(false) != 9;
}
