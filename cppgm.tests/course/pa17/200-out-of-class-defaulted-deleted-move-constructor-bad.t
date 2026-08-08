// N3485 focus: 8.4.2 [dcl.fct.def.default], 12.8 [class.copy]
// A user-provided explicitly-defaulted move constructor may not be deleted.

struct member
{
  member() = default;
  member(member&&) = delete;
};

struct owner
{
  member value;
  owner(owner&&);
};

owner::owner(owner&&) = default;

int main()
{
  return 0;
}
