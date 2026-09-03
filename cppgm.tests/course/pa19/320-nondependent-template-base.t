// Nondependent bases of class templates are validated at definition time;
// dependent bases and friend-granted private bases are accepted.

template<bool Value>
struct fixed_base
{
  static int read()
  {
    return Value ? 3 : 7;
  }
};

template<class T>
struct retained_only : fixed_base<false>
{
};

template<class T>
struct instantiated : fixed_base<true>
{
};

template<class T>
struct deferred : T
{
};

struct concrete
{
  static int dependent_read()
  {
    return 11;
  }
};

template<bool Value>
struct relation_base
{
  static int compare()
  {
    return Value ? 13 : 17;
  }
};

struct smaller
{
  enum { value = 1 };
};

struct larger
{
  enum { value = 2 };
};

template<class Left, class Right>
struct compared : relation_base<(Left::value < Right::value)>
{
};

class access_owner
{
  class hidden
  {
  };

  template<class>
  friend class privileged_derived;
};

template<class T>
class privileged_derived : access_owner::hidden
{
};

int main()
{
  instantiated<int> direct;
  deferred<concrete> dependent;
  compared<smaller, larger> comparison;
  privileged_derived<int> privileged;
  (void)privileged;
  return direct.read() == 3 && dependent.dependent_read() == 11 &&
    comparison.compare() == 13 ? 0 : 1;
}
