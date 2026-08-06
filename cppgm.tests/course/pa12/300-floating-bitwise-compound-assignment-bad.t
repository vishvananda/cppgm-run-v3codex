// VALIDATION: bitwise compound assignment requires integral operands.

void invalid()
{
  float value = 1.0f;
  value &= 2;
}
