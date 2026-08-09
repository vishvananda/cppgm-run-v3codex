// AUDIT: exception specifications own their contextual-bool conversion, and
// noexcept includes destruction of every materialized class temporary.
struct truth
{
  constexpr truth() {}
  constexpr explicit operator bool() const { return true; }
};

void selected() noexcept(truth());
static_assert(noexcept(selected()),
              "exception specification uses contextual conversion");

struct temporary
{
  temporary() noexcept {}
  ~temporary() noexcept(false) {}
};

static_assert(!noexcept(temporary()),
              "temporary destruction remains potentially throwing");

int main() { return 0; }
