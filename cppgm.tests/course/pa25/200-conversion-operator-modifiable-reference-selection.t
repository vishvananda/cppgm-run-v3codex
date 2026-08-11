struct Counter
{
  int mutable_value;
  int const_value;

  operator int &() { return mutable_value; }
  operator const int &() const { return const_value; }
};

int main()
{
  Counter counter = {4, 19};
  ++counter;
  return counter.mutable_value == 5 && counter.const_value == 19 ? 0 : 1;
}
