// VALIDATION: a lambda inherits the enclosing function's friend privilege.

class target
{
  friend struct reader;
  int value;
};

struct reader
{
  int read(target& object)
  {
    auto get = [&object]() { return object.value; };
    return get();
  }
};
