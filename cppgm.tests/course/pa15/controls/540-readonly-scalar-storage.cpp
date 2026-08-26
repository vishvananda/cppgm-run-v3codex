namespace
{
  const unsigned readonly_scalar = 9;
  const volatile unsigned volatile_scalar = 7;
  thread_local const unsigned thread_scalar = 5;

  struct Box
  {
    unsigned value;
  };

  const Box class_object = {3};
}

unsigned read_values()
{
  return readonly_scalar + volatile_scalar + thread_scalar +
    class_object.value;
}

int main()
{
  return read_values() == 24 ? 0 : 1;
}
