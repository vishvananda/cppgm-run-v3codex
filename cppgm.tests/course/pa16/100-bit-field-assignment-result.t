struct Bits
{
  unsigned first : 1;
  unsigned second : 2;
};

int main()
{
  Bits bits = {0, 0};
  return bits.second = 1;
}
