// AUDIT: an out-of-class definition of an in-class initialized static
// constant must reuse the canonical member and must not add an initializer.
struct holder
{
  static constexpr int values[2] = {1, 2};
};

constexpr int holder::values[2] = {3, 4};

int main() { return 0; }
