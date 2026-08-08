// N3485 13.3.1.2 [over.match.oper]: with only enumeration operands, a
// non-member operator is a candidate only when the corresponding parameter
// has that exact enumeration type (possibly through cv/reference wrapping).
namespace audit
{
enum class E { value };

struct box
{
  box(E);
};

int operator+(box, int);
}

int main()
{
  return audit::E::value + 1;
}
