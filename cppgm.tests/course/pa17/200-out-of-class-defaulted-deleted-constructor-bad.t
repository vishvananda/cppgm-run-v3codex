// N3485 focus: 8.4.2 [dcl.fct.def.default], 12.1 [class.ctor]
// A user-provided explicitly-defaulted constructor may not become deleted.

struct member
{
  member() = delete;
};

struct owner
{
  owner();
  member value;
};

owner::owner() = default;

int main()
{
  return 0;
}
