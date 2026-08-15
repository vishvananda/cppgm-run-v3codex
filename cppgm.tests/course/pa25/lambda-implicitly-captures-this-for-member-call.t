class owner
{
  int value() const
  {
    return 7;
  }

public:
  int call() const
  {
    auto invoke = [&]() { return value(); };
    return invoke();
  }
};

int use()
{
  owner object;
  return object.call();
}
