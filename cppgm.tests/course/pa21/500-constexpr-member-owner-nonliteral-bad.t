struct nonliteral
{
  ~nonliteral() {}

  constexpr int value() const
  {
    return 1;
  }
};

int main()
{
  return 0;
}
