struct root
{
  static int value;
};

int root::value = 1;

struct derived : root
{
  template<class T>
  static int value;
};

template<class T>
int derived::value = 2;

int main()
{
  return derived::value;
}
