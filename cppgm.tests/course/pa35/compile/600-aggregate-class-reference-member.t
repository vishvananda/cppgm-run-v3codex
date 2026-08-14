struct item {};

struct view
{
  const item& value;
};

view make_view(const item& value)
{
  return view{value};
}
