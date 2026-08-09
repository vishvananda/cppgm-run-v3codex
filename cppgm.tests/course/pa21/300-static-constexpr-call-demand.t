// AUDIT: compile-time-only scalar and address initializers must not create
// runtime emission demand for their selected constexpr functions.
struct holder
{
  static constexpr int make() { return 7; }
  static constexpr int value = make();
};

static_assert(holder::value == 7, "constant value is retained by identity");

static int source = 3;

constexpr int *address() { return &source; }

struct pointer_holder
{
  static constexpr int *value = address();
};

static_assert(pointer_holder::value == &source,
              "constant address is retained by identity");

struct entry
{
  int value;
  constexpr entry(int input) : value(input) {}
};

struct object_holder
{
  static constexpr entry value = entry(5);
};

static_assert(object_holder::value.value == 5,
              "constant object is retained by identity");

struct stored
{
  static constexpr int make() { return 9; }
  static constexpr int value = make();
};

constexpr int stored::value;

struct late
{
  static const int value;
};

const int late::value = 11;

static_assert(late::value == 11,
              "a first initializer belongs to the namespace definition");

int read(const int &value) { return value; }

int main() { return read(stored::value) - 9; }
