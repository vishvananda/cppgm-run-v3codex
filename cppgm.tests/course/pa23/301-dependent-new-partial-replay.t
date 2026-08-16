// PA23 focus: a dependent new-expression in a detection partial is checked
// after deduction, so an incomplete type discards only that candidate.

template<class...>
struct make_void
{
  typedef void type;
};

template<class... T>
using void_t = typename make_void<T...>::type;

template<class T, class = void>
constexpr bool can_new = false;

template<class T>
constexpr bool can_new<T, void_t<decltype(new T)> > = true;

struct complete {};
struct incomplete;

static_assert(can_new<complete>,
              "a complete type can be allocated");
static_assert(!can_new<incomplete>,
              "an incomplete type discards the detection candidate");

int main()
{
  return 0;
}
