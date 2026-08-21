// N3485 focus: 14.5.6 [temp.fct] An inline function-template specialization
// retains its optimization preference after instantiation.
template<class T>
inline T identity(T value)
{
  return value;
}

int use_identity(int value)
{
  return identity(value);
}
