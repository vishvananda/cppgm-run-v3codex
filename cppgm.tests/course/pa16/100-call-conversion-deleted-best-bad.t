// AUDIT: deletion is checked after choosing the best converting constructor.
struct X
{
  X(int) = delete;
  X(long) {}
};

void use(const X &) {}

int main()
{
  use(1);
  return 0;
}
