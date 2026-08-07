// AUDIT: a converting constructor cannot create a temporary for X&.
struct X
{
  X(int) {}
};

void use(X &) {}

int main()
{
  use(1);
  return 0;
}
