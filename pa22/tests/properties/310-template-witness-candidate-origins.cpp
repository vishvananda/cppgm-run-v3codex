// Template-witness candidate-origin relationship test.  The property runner
// checks declaration multiplicity without matching the complete witness.

template<class T>
struct pairish
{
  pairish(pairish const&) = default;
  pairish(pairish&&) = default;

  template<class A, class B, int = 0>
  pairish(A&&, B&&) {}
};

template<class T>
struct box
{
  T value;

  box& operator=(box const& other)
  {
    value = other.value;
    return *this;
  }

  template<class U>
  box& operator=(box<U> const& other)
  {
    value = other.value;
    return *this;
  }
};

int main()
{
  pairish<int> pair(1, 2);
  box<int> target;
  box<long> source;
  source.value = 7;
  target = source;
  return target.value == 7 ? 0 : 1;
}
