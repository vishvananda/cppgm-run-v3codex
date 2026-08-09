// AUDIT: partial selection accepts an ordinary incomplete class argument, and
// a cached shell retains its selected partial through a later definition.

class incomplete;

template<class T>
struct selected
{
  static const int value = 0;
};

template<class T>
struct selected<T const>
{
  static const int value = 1;
};

static_assert(selected<incomplete const>::value == 1,
              "ordinary incomplete arguments still participate in matching");

template<class T, class U>
struct late;

template<class T, class U>
struct late<T *, U>;

typedef late<int *, char> *cached_partial_shell;

template<class T, class U>
struct late<T *, U>
{
  static const int value = sizeof(U) + 10 * sizeof(T);
};

static_assert(late<int *, char>::value == 41,
              "the cached shell retains its selected partial owner");

int main()
{
  return 0;
}
