struct literal_base
{
  constexpr literal_base() {}
};

struct nonliteral_base
{
  nonliteral_base() {}
  ~nonliteral_base() {}
};

struct invalid : literal_base, nonliteral_base
{
  constexpr invalid() : literal_base(), nonliteral_base() {}
};

int main()
{
  return 0;
}
