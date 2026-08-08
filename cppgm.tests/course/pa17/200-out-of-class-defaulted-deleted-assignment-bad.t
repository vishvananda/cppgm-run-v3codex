// N3485 focus: 8.4.2 [dcl.fct.def.default], 12.8 [class.copy]
// A user-provided explicitly-defaulted assignment may not become deleted.

struct owner
{
  const int value;
  owner& operator=(owner&&);
};

owner& owner::operator=(owner&&) = default;

int main()
{
  return 0;
}
