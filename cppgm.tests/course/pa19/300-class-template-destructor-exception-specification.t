// N3485 focus: 12.4 [class.dtor] Each class-template specialization derives
// its implicit destructor exception-specification from concrete subobjects.
struct SafeLeaf
{
  ~SafeLeaf() noexcept {}
};

struct ThrowingLeaf
{
  ~ThrowingLeaf() noexcept(false) {}
};

template<class T>
struct Owner
{
  T value;
  ~Owner() {}
};

void destroy_safe(Owner<SafeLeaf> * value)
{
  value->~Owner();
}

void destroy_throwing(Owner<ThrowingLeaf> * value)
{
  value->~Owner();
}
