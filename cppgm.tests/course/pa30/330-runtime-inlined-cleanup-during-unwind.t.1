int cleanup_count;

struct cleanup
{
  ~cleanup() noexcept
  {
    ++cleanup_count;
  }
};

__attribute__((noinline)) void fail()
{
  throw 7;
}

int main()
{
  try
  {
    cleanup value;
    fail();
  }
  catch (int value)
  {
    return value == 7 && cleanup_count == 1 ? 0 : 1;
  }
  return 2;
}
