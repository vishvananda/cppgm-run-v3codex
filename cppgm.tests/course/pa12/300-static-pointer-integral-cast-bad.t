// VALIDATION: static_cast cannot perform a pointer-to-integer reinterpretation.

int invalid(int * value)
{
  return static_cast<int>(value);
}
