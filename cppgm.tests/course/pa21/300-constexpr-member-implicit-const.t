struct value
{
  int number;

  constexpr int read()
  {
    return number;
  }
};

constexpr value object = {3};

static_assert(object.read() == 3,
              "a C++11 constexpr member function is implicitly const");

int main()
{
  return 0;
}
