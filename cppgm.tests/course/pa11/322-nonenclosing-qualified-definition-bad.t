// N3485 7.3.1.2/2: a member of a named namespace may be defined outside it
// only in a namespace that encloses its own.  B does not enclose A.

namespace A
{
  extern int x;
}

namespace B
{
  int A::x;
}

int main() {}
