// VALIDATION: an enum pseudo-destructor call is a scalar no-op.

enum value_kind { expected = 7 };

void destroy(value_kind * value)
{
  value->~value_kind();
}

int main()
{
  value_kind value = expected;
  destroy(&value);
  return value == expected ? 0 : 1;
}
