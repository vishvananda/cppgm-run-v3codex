// N3485 focus: 8.4.2 [dcl.fct.def.default]
// An explicitly-defaulted assignment must have the implicit return type.

struct owner
{
  void operator=(owner&&);
};

void owner::operator=(owner&&) = default;

int main()
{
  return 0;
}
