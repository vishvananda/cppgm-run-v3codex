struct Wide
{
  unsigned char bits : 12;
  unsigned char tail;
};

int main()
{
  return sizeof(Wide);
}
