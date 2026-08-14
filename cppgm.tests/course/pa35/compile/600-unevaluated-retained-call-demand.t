template<class T> struct protector { static const bool stop = false; };

template<class T>
T&& source()
{
  static_assert(protector<T>::stop, "source body must remain lazy");
  return static_cast<T&&>(*static_cast<T*>(0));
}

struct value { int member(); };
using source_type = decltype(source<value>());
using member_type = decltype(source<value>().member());

static_assert(__is_same(source_type, value&&), "source category");
static_assert(__is_same(member_type, int), "member result");
