template<int N, class T, bool Selected>
struct exception_owner;

template<int N, class T>
struct exception_owner<N, T, true>
{
  exception_owner(int, long, short);
  exception_owner(T*, long, short) noexcept;
  exception_owner(char) noexcept(false);
};

template<int N, class U>
exception_owner<N, U, true>::exception_owner(U*, long, short) noexcept
{
}

template<int N, class U>
exception_owner<N, U, true>::exception_owner(char)
{
}

#ifndef FAMILY_COUNT
#define FAMILY_COUNT 16
#endif

#define CHECK_FAMILY(N)                                                     \
  static_assert(noexcept(exception_owner<N, int, true>(                     \
                  static_cast<int*>(0), 0L, static_cast<short>(0))),        \
                "the matching overload retains its nonthrowing fact");     \
  static_assert(!noexcept(exception_owner<N, int, true>('x')),              \
                "noexcept(false) matches an omitted throwing specification")

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

int main()
{
  return 0;
}
