int destroyed = 0;

struct value
{
  int number;

  value(int input) : number(input) {}
  ~value() { destroyed = destroyed + 1; }
};

value & get_value()
{
  static value item(7);
  return item;
}

int main()
{
  return get_value().number == 7 ? 0 : 1;
}
