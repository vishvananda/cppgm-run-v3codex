template<bool B, class T = void>
struct EnableIf {};

template<class T>
struct EnableIf<true, T> {
  typedef T type;
};

template<class A, class B>
struct IsSame {
  enum { value = 0 };
};

template<class A>
struct IsSame<A, A> {
  enum { value = 1 };
};

template<class T, class C>
struct MakeTransparent {
  typedef C type;
};

struct Marker {
  int value;
};

template<class T,
         class Compare,
         typename EnableIf<IsSame<Compare, typename MakeTransparent<T, Compare>::type>::value, int>::type = 0>
__attribute__((noinline))
Compare & as_transparent(Compare & comp)
{
  comp.value += 1;
  return comp;
}
