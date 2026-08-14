template<class T>
struct float_owner;

template<>
struct float_owner<_Float128>
{
  char value;
};

template<>
struct float_owner<__float128>
{
  char value[2];
};

int classify_float128(_Float128) { return 1; }
int classify_float128(__float128) { return 2; }

static_assert(sizeof(float_owner<_Float128>) == 1,
  "ISO extended float keeps its specialization identity");
static_assert(sizeof(float_owner<__float128>) == 2,
  "GNU float128 keeps its specialization identity");
