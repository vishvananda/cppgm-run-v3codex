struct member
{
  member() {}
  ~member() {}
};

struct holder
{
  holder() {}
  ~holder() {}

  union
  {
    member value;
  };
};

void use_holder()
{
  holder value;
}
