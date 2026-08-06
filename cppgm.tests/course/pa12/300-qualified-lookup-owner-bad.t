// VALIDATION: qualified lookup must not escape the nominated namespace.

void target();

namespace outer {
namespace empty {}

void test()
{
  empty::target();
}
}
