extern "C" int readonly_value(const int *value) noexcept
  __attribute__((pure));
extern "C" __attribute__((const)) int readnone_value(int value) noexcept;
extern "C" int ordinary_value(int value) noexcept;
extern "C" int potentially_throwing_value() __attribute__((pure));

template<class T>
__attribute__((const, noinline)) T template_value(T value) noexcept
{
  return value;
}

int use_effects(int value)
{
  readonly_value(&value);
  readnone_value(value);
  template_value(value);
  potentially_throwing_value();
  ordinary_value(value);
  return value;
}
