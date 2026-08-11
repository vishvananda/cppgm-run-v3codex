// N3485 focus: 14.8.2 [temp.deduct]. Substitution into an exception
// specification waits until the specialization's exception specification is
// needed; repeated queries consume the same completed semantic fact.

struct dormant_type
{};

template<class T>
auto dormant(T value)
  noexcept(sizeof(typename T::missing)) -> T;

using dormant_result = decltype(dormant(dormant_type()));

template<class T>
int measured(T) noexcept(sizeof(T) == sizeof(int))
{
  return 1;
}

static_assert(noexcept(measured(1)), "int specialization is nonthrowing");
static_assert(noexcept(measured(2)), "completed exception fact is reused");
static_assert(!noexcept(measured((char)0)),
              "char specialization is potentially throwing");

template<int N>
struct complete_tag
{};

template<int N>
int class_measured(complete_tag<N>)
  noexcept(sizeof(complete_tag<N>) != 0);

static_assert(noexcept(class_measured(complete_tag<0>())), "first class fact");
static_assert(noexcept(class_measured(complete_tag<0>())), "cached class fact");
static_assert(noexcept(class_measured(complete_tag<1>())), "second class fact");
static_assert(noexcept(class_measured(complete_tag<1>())), "cached class fact");
static_assert(noexcept(class_measured(complete_tag<2>())),
              "re-entrant class completion keeps the canonical binding");

int main()
{
  return 0;
}
