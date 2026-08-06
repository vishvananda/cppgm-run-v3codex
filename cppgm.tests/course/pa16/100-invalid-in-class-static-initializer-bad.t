// AUDIT: a mutable non-constexpr static member cannot be initialized in-class.

struct Holder
{
  static int value = 3;
};

int main()
{
  return Holder::value;
}
