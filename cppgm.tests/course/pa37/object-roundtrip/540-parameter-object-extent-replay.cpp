namespace
{
struct Fields
{
  long long head;
  long long middle;
  long long tail;

  __attribute__((noinline)) long long read_head() const noexcept;
  __attribute__((noinline)) long long read_middle_around_head() const noexcept;
};

long long Fields::read_head() const noexcept
{
  return head;
}

long long Fields::read_middle_around_head() const noexcept
{
  const long long before = middle;
  const long long observed = read_head();
  const long long after = middle;
  return before + observed + after;
}
}

int main()
{
  Fields value = { 3, 7, 11 };
  return value.read_middle_around_head() != 17;
}
