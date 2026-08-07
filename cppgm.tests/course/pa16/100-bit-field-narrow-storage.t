struct Bits
{
  unsigned char low : 3;
  unsigned char high : 3;
  unsigned char guard;
};

int main()
{
  Bits bits = {1, 2, 9};
  bits.high = 3;
  return bits.low + bits.high + bits.guard;
}
