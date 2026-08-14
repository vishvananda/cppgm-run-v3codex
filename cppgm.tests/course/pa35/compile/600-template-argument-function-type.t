template<class> struct function;

template<class R, class A>
struct function<R(A)> {
  typedef A argument_type;
};

template<class T>
using predicate = function<bool(T)>;

static_assert(sizeof(predicate<long>::argument_type) == sizeof(long),
              "function parameter stays a type");

template<bool> struct flag {};

template<int N>
using bool_value = flag<bool(N)>;

bool_value<0> zero;
bool_value<1> one;
