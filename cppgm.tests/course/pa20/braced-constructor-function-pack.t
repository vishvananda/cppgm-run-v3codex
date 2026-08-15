struct box
{
  box(int, int, int) {}
};

template<typename... Values>
void build_box(Values... values)
{
  box value{0, values...};
  (void)value;
}

void use_box()
{
  build_box(1, 2);
}
