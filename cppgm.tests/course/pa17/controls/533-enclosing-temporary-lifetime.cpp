int alive;
bool choose_second = true;

struct value
{
  value() { ++alive; }
  ~value() { --alive; }
};

int operator+(const value &, const value &)
{
  return alive == 2 ? 0 : 1;
}

int main()
{
  int result = value() + (choose_second ? value() : value());
  return result != 0 || alive != 0;
}
