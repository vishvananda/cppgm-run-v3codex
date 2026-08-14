template<class T> struct traits;

template<> struct traits<int>
{
  static void assign(int& left, const int& right) { left = right; }
};

template<> struct traits<long>
{
  static void assign(long& left, const long& right) { left = right; }
};

template<class T, class Traits = traits<T> >
struct box
{
  template<class U>
  static void copy(T* destination, U source)
  {
    Traits::assign(*destination, static_cast<T>(source));
  }
};

void use()
{
  int first = 0;
  long second = 0;
  box<int>::copy(&first, 1);
  box<long>::copy(&second, 2);
}
