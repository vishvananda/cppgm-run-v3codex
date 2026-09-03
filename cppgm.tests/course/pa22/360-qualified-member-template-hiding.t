// A function template declared in a class hides a same-named base member in
// qualified lookup; the derived template must be selected.

struct root
{
  static int choose(int)
  {
    return 1;
  }
};

struct derived : root
{
  template<class T>
  static int choose(T)
  {
    return 2;
  }
};

int main()
{
  return derived::choose(0) == 2 ? 0 : 1;
}
