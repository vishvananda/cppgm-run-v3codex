struct nonliteral
{
  int value;

  constexpr nonliteral() : value(1) {}
  ~nonliteral() {}
};

constexpr nonliteral object;

int main()
{
  return 0;
}
