namespace std {
template<class T>
class initializer_list {
  const T* first_;
  unsigned long size_;
public:
  initializer_list() noexcept : first_(0), size_(0) {}
  const T* begin() const noexcept { return first_; }
  unsigned long size() const noexcept { return size_; }
};
}

template<class T>
using list_alias = std::initializer_list<T>;

#ifndef FAMILY_COUNT
#define FAMILY_COUNT 16
#endif

#define EXERCISE(N, TYPE) \
  using alias_##N = list_alias<TYPE>; \
  std::initializer_list<TYPE> direct_##N; \
  std::initializer_list<TYPE> repeated_##N; \
  alias_##N values_##N; \
  result += direct_##N.size() != 0 || repeated_##N.size() != 0 || \
    values_##N.size() != 0

int main() {
  int result = 0;
#if FAMILY_COUNT >= 1
  EXERCISE(1, int);
#endif
#if FAMILY_COUNT >= 2
  EXERCISE(2, long);
#endif
#if FAMILY_COUNT >= 3
  EXERCISE(3, unsigned);
#endif
#if FAMILY_COUNT >= 4
  EXERCISE(4, short);
#endif
#if FAMILY_COUNT >= 5
  EXERCISE(5, char);
#endif
#if FAMILY_COUNT >= 6
  EXERCISE(6, bool);
#endif
#if FAMILY_COUNT >= 7
  EXERCISE(7, float);
#endif
#if FAMILY_COUNT >= 8
  EXERCISE(8, double);
#endif
#if FAMILY_COUNT >= 9
  EXERCISE(9, int*);
#endif
#if FAMILY_COUNT >= 10
  EXERCISE(10, const int*);
#endif
#if FAMILY_COUNT >= 11
  EXERCISE(11, long*);
#endif
#if FAMILY_COUNT >= 12
  EXERCISE(12, unsigned*);
#endif
#if FAMILY_COUNT >= 13
  EXERCISE(13, short*);
#endif
#if FAMILY_COUNT >= 14
  EXERCISE(14, char*);
#endif
#if FAMILY_COUNT >= 15
  EXERCISE(15, void*);
#endif
#if FAMILY_COUNT >= 16
  EXERCISE(16, double*);
#endif
  return result;
}
