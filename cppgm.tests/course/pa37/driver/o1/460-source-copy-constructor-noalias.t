struct Pair
{
  long first;
  long second;
  long third;

  Pair(long left, long middle, long right)
    : first(left), second(middle), third(right) {}

  __attribute__((noinline)) Pair(const Pair& other)
  {
    first = other.first;
    second = other.second;
    third = other.third;
  }
};

int main()
{
  Pair source(5, 17, 20);
  Pair destination(source);
  return destination.first + destination.second + destination.third - 42;
}
