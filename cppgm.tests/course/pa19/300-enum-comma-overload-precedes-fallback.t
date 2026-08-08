// N3485 13.3.1.2 [over.match.oper]: built-in comma is a fallback when no
// viable overload exists, not a candidate competing with a viable overload.
enum E { value };

long operator,(E, long)
{
  return 9;
}

int main()
{
  return (value, 1) == 9 ? 0 : 1;
}
