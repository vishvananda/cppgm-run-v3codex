// PA23 retains dependent function-template result meaning across equivalent
// redeclarations, including when many unrelated result shapes share one TU.

template<class T, int N>
struct selected
{
  typedef T type;
};

#define DECLARE_RESULT(N)                                                   \
  template<class T> typename selected<T, N>::type result_##N(T);           \
  template<class T> typename selected<T, N>::type result_##N(T)

DECLARE_RESULT(0);
DECLARE_RESULT(1);
DECLARE_RESULT(2);
DECLARE_RESULT(3);
DECLARE_RESULT(4);
DECLARE_RESULT(5);
DECLARE_RESULT(6);
DECLARE_RESULT(7);
DECLARE_RESULT(8);
DECLARE_RESULT(9);
DECLARE_RESULT(10);
DECLARE_RESULT(11);
DECLARE_RESULT(12);
DECLARE_RESULT(13);
DECLARE_RESULT(14);
DECLARE_RESULT(15);
DECLARE_RESULT(16);
DECLARE_RESULT(17);
DECLARE_RESULT(18);
DECLARE_RESULT(19);
DECLARE_RESULT(20);
DECLARE_RESULT(21);
DECLARE_RESULT(22);
DECLARE_RESULT(23);

static_assert(sizeof(result_0(0)) == sizeof(int), "first result changed");
static_assert(sizeof(result_11(0)) == sizeof(int), "middle result changed");
static_assert(sizeof(result_23(0)) == sizeof(int), "last result changed");

int main()
{
  return 0;
}
