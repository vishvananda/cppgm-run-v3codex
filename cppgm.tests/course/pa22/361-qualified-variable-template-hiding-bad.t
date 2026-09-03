// A variable template named without template arguments is invalid; lookup must
// not fall through to a hidden base data member.

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
