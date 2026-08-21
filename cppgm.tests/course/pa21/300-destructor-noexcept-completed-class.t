// N3485 focus: 12.4 [class.dtor], 15.4 [except.spec] Destructor noexcept
// queries use the completed class's direct base and member destructors.
struct SafeLeaf
{
  ~SafeLeaf() noexcept {}
};

struct ThrowingLeaf
{
  ~ThrowingLeaf() noexcept(false) {}
};

struct SafeOwner
{
  SafeLeaf leaf;
  ~SafeOwner() {}
};

struct ThrowingOwner
{
  ThrowingLeaf leaf;
  ~ThrowingOwner() {}
};

struct ImplicitThrowingOwner
{
  ThrowingLeaf leaf;
};

static_assert(noexcept(((SafeOwner *)0)->~SafeOwner()),
              "a nonthrowing member makes the owner destructor nonthrowing");
static_assert(!noexcept(((ThrowingOwner *)0)->~ThrowingOwner()),
              "a throwing member makes a user destructor potentially throwing");
static_assert(!noexcept(((ImplicitThrowingOwner *)0)->~ImplicitThrowingOwner()),
              "an implicit destructor follows a throwing member");

int main() { return 0; }
