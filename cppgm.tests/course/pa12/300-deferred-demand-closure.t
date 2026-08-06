struct Dependency {};

struct Owner {
  void selected()
  {
    Dependency value;
  }
};

typedef void (Owner::* member_function)();

member_function take()
{
  return &Owner::selected;
}
