// N3485 focus: 14.5.6.1 [temp.over.link] and 14.6.3 [temp.nondep].
// A function-template result retains the first declaration's lookup even when
// a later declaration changes the enclosing lookup set.

template<bool, class T> struct gate {};
template<class T> struct gate<true, T> { typedef T type; };

namespace catalog {
template<bool, class T> struct gate {};
template<class T> struct gate<true, T> { typedef T type; };
} // namespace catalog

namespace nested {

template<class T>
typename gate<(sizeof(T) > 0), long>::type rooted(T);

template<bool, class T> struct gate {};
template<class T> struct gate<true, T> { typedef T type; };

template<class T>
typename ::gate<(sizeof(T) > 0), long>::type rooted(T);

template<class T>
typename catalog::gate<(sizeof(T) > 0), long>::type qualified(T);

template<class T>
typename ::catalog::gate<(sizeof(T) > 0), long>::type qualified(T);

} // namespace nested

long selected(double);

template<class T>
auto selected_at_first_declaration(T) -> decltype(selected(0));

int selected(int);

template<class T>
auto selected_at_first_declaration(T) -> decltype(selected(0));

static_assert(sizeof(nested::rooted(0)) == sizeof(long),
              "qualified-equivalent results keep the first type binding");
static_assert(sizeof(nested::qualified(0)) == sizeof(long),
              "global qualification preserves namespace identity");
static_assert(sizeof(selected_at_first_declaration(0)) == sizeof(long),
              "a later overload is absent from the retained call set");

int main()
{
  return 0;
}
