struct base
{
  virtual base * clone();
};

struct derived : base
{
  virtual int * clone();
};

int main() { return 0; }
