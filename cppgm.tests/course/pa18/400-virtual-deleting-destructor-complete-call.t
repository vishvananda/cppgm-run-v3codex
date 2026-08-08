int observed;

struct member
{
  ~member() { observed = 1; }
};

struct base
{
  member value;
  virtual ~base() { observed = 2; }
};

int main()
{
  delete (base *)0;
  return observed;
}
