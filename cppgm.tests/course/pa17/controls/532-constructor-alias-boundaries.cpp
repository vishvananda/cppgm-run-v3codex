struct Pair
{
  long value;

  Pair(long initial) : value(initial) {}

  __attribute__((noinline)) Pair(const Pair & other)
    : value(other.value) {}

  __attribute__((noinline)) Pair(Pair && other)
    : value(other.value)
  {
    other.value = 0;
  }

  __attribute__((noinline)) Pair & operator=(const Pair & other)
  {
    value = other.value;
    return *this;
  }
};

int main()
{
  Pair source(14);
  Pair copied(source);
  Pair moved(static_cast<Pair &&>(copied));
  copied = moved;
  return source.value + moved.value + copied.value == 42 ? 0 : 1;
}
