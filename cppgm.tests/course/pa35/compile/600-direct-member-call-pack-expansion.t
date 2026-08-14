struct sink
{
  int total;
  sink() : total(0) {}
  void add(int first, int second) { total = first + second; }
};

template<class... T>
int apply(sink& target, T... values)
{
  target.add(values...);
  return target.total;
}

int use()
{
  sink value;
  return apply(value, 2, 3);
}
