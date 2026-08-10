// N3485 focus: 10 [class.derived] and 14.6 [temp.res].
// A retained decltype base is identified by syntax, not by a spelling prefix
// that can collide with an ordinary class name.

struct decltype_base {};
struct derived : decltype_base {};

int main()
{
  derived value;
  (void)value;
  return 0;
}
