// AUDIT: thread storage duration does not erase namespace internal linkage.

static thread_local int value = 3;

int main()
{
  return value - 3;
}
