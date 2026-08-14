typedef unsigned long count_type;

namespace first {
typedef unsigned long count_type;
}

namespace second {
typedef unsigned long count_type;
}

using namespace first;
using namespace second;

count_type count_value = 0;
static_assert(sizeof(count_value) == sizeof(unsigned long),
              "same-type namespace aliases converge");

int main()
{
  return count_value != 0;
}
