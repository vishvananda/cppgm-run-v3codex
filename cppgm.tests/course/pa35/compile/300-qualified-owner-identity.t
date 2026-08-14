template<class T>
struct box
{
  T value;
};

template<int N, class T, bool Selected>
struct selected_owner;

template<int N, class T>
struct selected_owner<N, T, true>
{
  typedef T value_type;
  box<value_type> make() const;
};

template<int N, class T>
box<typename selected_owner<N, T, true>::value_type>
selected_owner<N, T, true>::make() const
{
  return box<value_type>();
}

template<int N, class T>
struct friend_owner;

template<int N, class T>
struct friend_leaf
{
  friend struct friend_owner<N, T>;
  T* pointer;
};

template<int N, class T>
struct friend_owner
{
  friend_leaf<N, T> leaf;
};

#ifndef FAMILY_COUNT
#define FAMILY_COUNT 16
#endif

#define CHECK_FAMILY(N)                                                     \
  static_assert(sizeof(typename selected_owner<N, int, true>::value_type) ==\
                  sizeof(int),                                              \
                "selected partial owner publishes its canonical alias");   \
  static_assert(sizeof(friend_leaf<N, int>) == sizeof(int*),                \
                "friend lookup does not demand its recursive owner")

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

int function_target()
{
  return 7;
}

template<class F>
int invoke(F&& function)
{
  return function();
}

int main()
{
  selected_owner<1, int, true> owner;
  return owner.make().value + sizeof(friend_owner<1, int>) +
         invoke(function_target);
}
