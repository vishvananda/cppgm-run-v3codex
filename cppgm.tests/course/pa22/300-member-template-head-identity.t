// AUDIT: function-template identity includes the template-parameter kinds;
// equal function types do not merge type and non-type member templates.
// Dependent value-parameter types compare structurally with renamed parameters.

template<class Host>
struct owner
{
  template<class U>
  int choose();

  template<int I>
  int choose();

  template<class T, T I>
  int typed();

  template<class T, decltype(sizeof(T)) I>
  int typed();
};

template<class Host>
template<class U>
int owner<Host>::choose()
{
  return 1;
}

template<class Host>
template<int I>
int owner<Host>::choose()
{
  return I;
}

template<class Host>
template<class Value, Value Index>
int owner<Host>::typed()
{
  return Index;
}

template<class Host>
template<class Value, decltype(sizeof(Value)) Index>
int owner<Host>::typed()
{
  return Index;
}

int main()
{
  owner<char> value;
  return value.choose<long>() == 1 && value.choose<7>() == 7 ? 0 : 1;
}
