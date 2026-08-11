// VALIDATION: compile-fail

struct base { virtual int key() { return 0; } };
struct derived : private base {};

base *reject(derived *source)
{
  return dynamic_cast<base *>(source);
}
