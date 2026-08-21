#include <string>

static_assert(noexcept(((std::string *)0)->~basic_string()),
              "basic_string has an implicit nonthrowing destructor specification");

int main()
{
  std::string value("ok");
  return value.size() == 2 ? 0 : 1;
}
