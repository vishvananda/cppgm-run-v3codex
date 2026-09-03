// Positive control for the four adjacent rules exercised by the 320-323
// negatives, so those diagnostics cannot be satisfied by over-rejecting.

// void is legal as a return type and as a pointee (N3485 3.9.1/9 restricts
// only objects of type void).
void returns_nothing();
void* pointer_to_void = 0;

// A reference may omit its initializer with an explicit extern, as a class
// member, as a parameter, and as a return type (N3485 8.3.2/5).
int subject = 7;
extern int& external_reference;
struct holder
{
  int& member;
  holder(int& bound) : member(bound) {}
};
int& returns_reference();
int takes_reference(int& parameter);

// A qualified definition is well-formed in a namespace enclosing the entity's
// own namespace (N3485 7.3.1.2/2), including the global namespace.
namespace outer
{
  extern int direct;
  namespace inner { extern int nested; }
  struct owner { static int member; };
}
int outer::direct = 1;
namespace outer { int inner::nested = 2; }
int outer::owner::member = 3;

// A namespace alias may be redeclared to the same namespace, and a real
// namespace may still be reopened (N3485 7.3.2).
namespace alias_target { int value = 4; }
namespace alias_name = alias_target;
namespace alias_name = alias_target;
namespace alias_target { int second = 5; }

// A reference initialized with a constant expression is usable as one, so it
// may size an array (N3485 5.19/2).
const unsigned long extent = 3;
const unsigned long& extent_reference = extent;
const unsigned long& chained_reference = extent_reference;
int sized_by_reference[extent_reference];
int sized_by_chain[chained_reference];

int main()
{
  holder local(subject);
  return local.member + takes_reference(subject) - 14;
}
