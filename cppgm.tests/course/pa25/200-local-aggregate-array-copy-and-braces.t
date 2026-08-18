struct Element
{
  int value;
  int nested[1];
};

int copied_nested()
{
  Element source = {1, {2}};
  const Element values[] = {source};
  return values[0].nested[0];
}
