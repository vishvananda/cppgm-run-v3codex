// N3485 7.3.2: a namespace-alias names an existing namespace; it is not itself
// a namespace and cannot be extended by a namespace-definition.

namespace A
{
  typedef int T;
}

namespace B = A;

namespace B { }

int main() {}
