extern "C" void observe(int value) noexcept;

inline void preferred(int value)
{
  observe(value);
  observe(value);
  observe(value);
  observe(value);
  observe(value);
  observe(value);
}

int use_preferred(int value)
{
  preferred(value);
  return value;
}
