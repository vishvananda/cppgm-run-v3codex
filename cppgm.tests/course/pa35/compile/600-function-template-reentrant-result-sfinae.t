template<class T> T&& declval();

template<class Os, class T, class = void,
         class = decltype(declval<Os&>() << declval<const T&>())>
using stream_result = Os&&;

template<class Os, class T>
stream_result<Os, T> operator<<(Os&& out, const T& value);

struct stream {
  stream& operator<<(int);
};

using inserted = decltype(declval<stream&>() << declval<int>());

struct blocked {};

template<class T>
char probe(decltype(declval<T&>() << declval<int>())*);

template<class>
long probe(...);

static_assert(sizeof(probe<blocked>(0)) == sizeof(long),
              "recursive candidate fails substitution without an inserter");
