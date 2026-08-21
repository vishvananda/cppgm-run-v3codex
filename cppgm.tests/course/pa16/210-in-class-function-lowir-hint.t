// N3485 focus: 9.3 [class.mfct] A function defined in its class definition is
// an inline function and carries the same optimization preference.
struct Box
{
  int value;

  int get()
  {
    return value;
  }
};

int read(Box * box)
{
  return box->get();
}
