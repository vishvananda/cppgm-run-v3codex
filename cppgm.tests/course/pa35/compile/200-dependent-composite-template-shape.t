template<class T>
struct same
{
  static const bool value = false;
};

template<class T>
struct same<T(T)>
{
  static const bool value = true;
};

template<class T>
struct function_shape_owner
{
  typedef same<T(T)> shape;
};

struct member_owner
{
  int value;
};

template<class T>
struct member_pointer_shape_owner
{
  typedef same<int T::*> shape;
};

template<class T>
struct must_remain_lazy
{
  static_assert(sizeof(T) != sizeof(T),
                "dependent composite specialization was completed eagerly");
};

template<class T>
struct lazy_composite_owner
{
  typedef must_remain_lazy<void(T)> function_shape;
  typedef must_remain_lazy<int T::*> member_pointer_shape;
};

lazy_composite_owner<member_owner>* lazy_owner;

template<class T>
struct is_function
{
  static const bool value = false;
};

template<class R, class A>
struct is_function<R(A)>
{
  static const bool value = true;
};

template<int N>
struct shape_tag
{
};

template<int N>
struct destructor_leaf
{
  ~destructor_leaf() = default;
};

template<int N, class T>
struct composite_family
{
  typedef is_function<T(shape_tag<N>)> shape;
  destructor_leaf<N> leaf;
  ~composite_family() = default;
};

#ifndef FAMILY_COUNT
#define FAMILY_COUNT 16
#endif

#define CHECK_FAMILY(N)                                                     \
  static_assert(composite_family<N, int>::shape::value,                     \
                "dependent function shape resolves once concrete");        \
  static_assert(noexcept(composite_family<N, int>()),                       \
                "nested defaulted destructor stays nonthrowing")

#if FAMILY_COUNT >= 1
CHECK_FAMILY(1);
#endif
#if FAMILY_COUNT >= 2
CHECK_FAMILY(2);
#endif
#if FAMILY_COUNT >= 3
CHECK_FAMILY(3);
#endif
#if FAMILY_COUNT >= 4
CHECK_FAMILY(4);
#endif
#if FAMILY_COUNT >= 5
CHECK_FAMILY(5);
#endif
#if FAMILY_COUNT >= 6
CHECK_FAMILY(6);
#endif
#if FAMILY_COUNT >= 7
CHECK_FAMILY(7);
#endif
#if FAMILY_COUNT >= 8
CHECK_FAMILY(8);
#endif
#if FAMILY_COUNT >= 9
CHECK_FAMILY(9);
#endif
#if FAMILY_COUNT >= 10
CHECK_FAMILY(10);
#endif
#if FAMILY_COUNT >= 11
CHECK_FAMILY(11);
#endif
#if FAMILY_COUNT >= 12
CHECK_FAMILY(12);
#endif
#if FAMILY_COUNT >= 13
CHECK_FAMILY(13);
#endif
#if FAMILY_COUNT >= 14
CHECK_FAMILY(14);
#endif
#if FAMILY_COUNT >= 15
CHECK_FAMILY(15);
#endif
#if FAMILY_COUNT >= 16
CHECK_FAMILY(16);
#endif

#undef CHECK_FAMILY

static_assert(function_shape_owner<int>::shape::value,
              "dependent function type reaches its concrete specialization");
static_assert(!member_pointer_shape_owner<member_owner>::shape::value,
              "dependent member-pointer owner reaches its concrete specialization");

int main()
{
  return lazy_owner != 0;
}
