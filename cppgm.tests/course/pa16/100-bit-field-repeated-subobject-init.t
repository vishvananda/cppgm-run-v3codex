struct Bits
{
  unsigned first : 1;
  unsigned second : 2;
};

struct Pair
{
  Bits left;
  Bits right;
};

int main()
{
  Pair pair = {{1, 2}, {0, 3}};
  return pair.right.first + pair.right.second;
}
