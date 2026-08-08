// N3485 focus: 8.4.2 [dcl.fct.def.default], 12.4 [class.dtor]
// A union destructor defaulted after its first declaration may not be deleted.

struct member
{
  ~member() {}
};

union owner
{
  member value;
  ~owner();
};

owner::~owner() = default;

int main()
{
  return 0;
}
