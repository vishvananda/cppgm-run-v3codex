template<class T> struct protector { static const bool stop = false; };

template<class T>
T&& source()
{
  static_assert(protector<T>::stop, "evaluated source must demand its body");
  return static_cast<T&&>(*static_cast<T*>(0));
}

struct value { int member(); };

int force_source_body()
{
  return source<value>().member();
}
