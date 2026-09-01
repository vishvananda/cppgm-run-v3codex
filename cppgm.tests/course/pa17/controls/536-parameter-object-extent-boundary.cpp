struct Triple
{
  long long first;
  long long second;
  long long third;

  Triple(long long base) : first(base), second(base + 1), third(base + 2) {}

  long long total() const
  {
    return first + second + third;
  }
};

extern "C" __attribute__((noinline))
long long reference_total(const Triple& value)
{
  return value.total();
}

extern "C" __attribute__((noinline))
Triple make_triple(long long base)
{
  return Triple(base);
}

extern "C" __attribute__((noinline))
long long pointer_value(const long long* value)
{
  return *value;
}

int main()
{
  Triple value = make_triple(5);
  long long (Triple::*member)() const = &Triple::total;
  return reference_total(value) != 18 || pointer_value(&value.second) != 6 ||
    (value.*member)() != 18;
}
