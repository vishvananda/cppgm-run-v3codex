// N3485 focus: 7.1.2 [dcl.fct.spec] The inline specifier is retained as an
// optimization preference independently of linkage and object retention.
inline int preferred(int value)
{
  return value + 1;
}

int use_preferred(int value)
{
  return preferred(value);
}
