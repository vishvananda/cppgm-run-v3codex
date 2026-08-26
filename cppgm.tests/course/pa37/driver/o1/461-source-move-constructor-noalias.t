struct Head
{
  long value;

  Head(long initial) : value(initial) {}

  __attribute__((noinline)) Head(Head&& other)
    : value(other.value)
  {
    other.value = 0;
  }
};

struct Bundle
{
  Head head;
  long first;
  long second;
  long third;

  Bundle(long h, long a, long b, long c)
    : head(h), first(a), second(b), third(c) {}

  __attribute__((noinline)) Bundle(Bundle&&) = default;
};

int main()
{
  Bundle source(1, 4, 16, 21);
  Bundle destination(static_cast<Bundle&&>(source));
  return destination.head.value + destination.first +
    destination.second + destination.third - 42;
}
