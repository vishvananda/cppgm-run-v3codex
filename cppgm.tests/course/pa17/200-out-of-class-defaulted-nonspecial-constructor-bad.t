// N3485 focus: 8.4.2 [dcl.fct.def.default]
// An explicitly-defaulted function must be a special member function.

struct bad
{
  bad(int);
};

bad::bad(int) = default;

int main()
{
  return 0;
}
