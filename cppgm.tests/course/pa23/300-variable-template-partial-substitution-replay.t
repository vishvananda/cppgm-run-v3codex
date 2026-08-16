// PA23 focus: substitution failure discards a variable-template partial
// specialization whose dependent void_t argument is ill-formed.

template<class...>
struct make_void
{
  typedef void type;
};

template<class... T>
using void_t = typename make_void<T...>::type;

template<class T>
T&& value();

template<class Owner, class Item, class = void>
constexpr bool has_destroy = false;

template<class Owner, class Item>
constexpr bool has_destroy<Owner, Item,
  void_t<decltype(value<Owner&>().destroy(value<Item*>()))> > = true;

struct owner
{
  void destroy(int*);
};

static_assert(!has_destroy<int, int>,
              "an invalid detection candidate must be discarded");
static_assert(has_destroy<owner, int>,
              "a valid detection candidate must still be selected");

int main()
{
  return 0;
}
