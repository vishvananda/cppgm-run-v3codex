struct Empty
{
  Empty() noexcept {}
  Empty(const Empty &) noexcept {}
};

struct Data
{
  void * first;
  void * second;
  void * third;

  Data() : first(0), second(0), third(0) {}
  Data(Data && other) noexcept
    : first(other.first), second(other.second), third(other.third) {}
};

struct Impl : Empty, Data
{
  Impl() {}
  Impl(Impl && other) noexcept
    : Empty(static_cast<Impl &&>(other)),
      Data(static_cast<Impl &&>(other)) {}
};

int main()
{
  Impl source;
  Impl destination(static_cast<Impl &&>(source));
  return destination.first != 0;
}

// VALIDATION: compile-pass
