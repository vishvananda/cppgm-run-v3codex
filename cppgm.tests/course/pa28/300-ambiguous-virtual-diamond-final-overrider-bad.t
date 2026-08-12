struct root
{
  virtual int value();
};

struct left : virtual root
{
  int value() override;
};

struct right : virtual root
{
  int value() override;
};

struct ambiguous : left, right {};

int main()
{
  ambiguous object;
  return 0;
}
