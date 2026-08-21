// N3485 focus: 12.4 [class.dtor] A destructor without an explicit
// exception-specification uses the specification of its subobject destructors.
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

void destroy_safe(SafeOwner * value)
{
  value->~SafeOwner();
}

void destroy_throwing(ThrowingOwner * value)
{
  value->~ThrowingOwner();
}
