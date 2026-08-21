// N3485 focus: 12.4 [class.dtor] An explicit specialization of a destructor
// without an exception-specification retains the subobject-derived boundary.
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
  ~Owner();
};

template<class T>
Owner<T>::~Owner() {}

template<>
Owner<SafeLeaf>::~Owner() {}

template<>
Owner<ThrowingLeaf>::~Owner() {}

void destroy_safe(Owner<SafeLeaf> * value)
{
  value->~Owner();
}

void destroy_throwing(Owner<ThrowingLeaf> * value)
{
  value->~Owner();
}
