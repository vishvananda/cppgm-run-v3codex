// N3485 focus: 11.4 [class.protected] permits a member of a derived class to
// name an accessible protected constructor of its base class.
struct protected_base
{
protected:
  int value;
  constexpr protected_base(int input) : value(input) {}

public:
  constexpr int read() const { return value; }
};

struct derived_value : protected_base
{
  constexpr derived_value() : protected_base(17) {}
};

static_assert(derived_value().read() == 17,
              "constexpr base initialization keeps derived-class access");

int main() { return 0; }
