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

template<class T>
struct reader
{
  template<class U>
  U get(U value) { return value; }
};

template<class T>
int concrete_replay()
{
  reader<int> value;
  return value.get<int>(4);
}

template<class T>
int dependent_replay(T& value)
{
  return value.template get<int>(5);
}

int main()
{
  pairish<int> pair(1, 2);
  box<int> target;
  box<long> source;
  source.value = 7;
  target = source;
  reader<int> value;
  return target.value == 7 && concrete_replay<void>() == 4 &&
    dependent_replay(value) == 5 ? 0 : 1;
}
