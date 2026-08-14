struct base {};

template<class T>
struct derived : base {};

template<class T>
derived<T>& downcast(base& value)
{
  return static_cast<derived<T>&>(value);
}

derived<int>& use(base& value)
{
  return downcast<int>(value);
}
