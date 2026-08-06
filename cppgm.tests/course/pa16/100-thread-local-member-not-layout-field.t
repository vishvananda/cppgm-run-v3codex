// AUDIT: a static thread_local member is not a class subobject.

struct Holder
{
  static thread_local int value;
  char byte;
};

thread_local int Holder::value = 3;

int main()
{
  return sizeof(Holder) - 1;
}
