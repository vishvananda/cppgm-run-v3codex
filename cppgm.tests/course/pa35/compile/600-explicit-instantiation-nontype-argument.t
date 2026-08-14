template<class T, bool Enabled>
struct explicit_owner
{
  T value;
};

extern template struct explicit_owner<int, false>;
template struct explicit_owner<int, true>;

static_assert(sizeof(explicit_owner<int, false>) == sizeof(int),
  "extern class instantiation accepts a value argument");
static_assert(sizeof(explicit_owner<int, true>) == sizeof(int),
  "class instantiation definition accepts a value argument");
