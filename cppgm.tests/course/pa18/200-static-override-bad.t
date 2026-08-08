struct base
{
  virtual int value();
};

struct derived : base
{
  static int value() override;
};

int main() { return 0; }
