template<class T>
struct holder
{
  void use() noexcept(sizeof(T) != 0);
};

struct recursive
{
  holder<recursive> value;
};

static_assert(noexcept(((recursive*)0)->value.use()),
              "the exception specification is valid after completion");
static_assert(noexcept(((recursive*)0)->value.use()),
              "repeated demand reuses the completed exception fact");
