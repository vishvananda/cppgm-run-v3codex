struct base
{
  virtual base * clone();
};

struct derived : base
{
  virtual derived const * clone();
};

int main() { return 0; }
