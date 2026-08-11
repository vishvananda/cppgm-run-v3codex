// VALIDATION: compile-fail

struct value { int member; };

value *reject(const value *source)
{
  return dynamic_cast<value *>(source);
}
