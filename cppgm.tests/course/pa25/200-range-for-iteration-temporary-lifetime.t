int destructions;

struct Step
{
  Step() {}
  ~Step() { ++destructions; }
};

struct Iterator
{
  int* current;

  int& operator*() const { return *current; }
  Step operator++() { ++current; return Step(); }
  bool operator!=(const Iterator& other) const
  {
    return current != other.current;
  }
};

struct Range
{
  int values[2];
  Iterator begin() { return Iterator{values}; }
  Iterator end() { return Iterator{values + 2}; }
};

int main()
{
  Range range = {{4, 5}};
  int sum = 0;
  for (int value : range)
    sum = sum + value;
  return destructions == 2 && sum == 9 ? 0 : 1;
}
