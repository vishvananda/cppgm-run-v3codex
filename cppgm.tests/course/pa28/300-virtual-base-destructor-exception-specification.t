// N3485 focus: 12.4 [class.dtor], 15.4 [except.spec] A most-derived
// destructor includes virtual-base destructors in its implicit specification.
struct SafeVirtual
{
  ~SafeVirtual() noexcept {}
};

struct ThrowingVirtual
{
  ~ThrowingVirtual() noexcept(false) {}
};

struct SafePath : virtual SafeVirtual {};
struct ThrowingPath : virtual ThrowingVirtual {};

struct SafeOwner : SafePath
{
  ~SafeOwner() {}
};

struct ThrowingOwner : ThrowingPath
{
  ~ThrowingOwner() {}
};

static_assert(noexcept(((SafeOwner *)0)->~SafeOwner()),
              "safe virtual base destructor");
static_assert(!noexcept(((ThrowingOwner *)0)->~ThrowingOwner()),
              "throwing virtual base destructor");

int main() { return 0; }
