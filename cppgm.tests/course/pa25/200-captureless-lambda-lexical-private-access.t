struct holder
{
private:
  int value;

public:
  holder() : value(7) {}

  int read()
  {
    auto get = [](holder& object) { return object.value; };
    return get(*this);
  }
};

int main()
{
  holder object;
  return object.read() - 7;
}
