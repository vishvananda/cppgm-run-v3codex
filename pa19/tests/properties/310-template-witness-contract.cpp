// Template-witness relationship test.  This file is consumed by
// scripts/check_template_witness_contract.pl rather than the LowIR golden
// runner so the assertions concern semantic relationships, not exact output.

template<class T, class U = long>
struct box
{
  T value;
};

template<class T, class U>
int inspect(box<T, U>& value)
{
  return sizeof(value.value);
}

template<class T>
int shadowed(T)
{
  return 1;
}

int shadowed(int)
{
  return 0;
}

int main()
{
  box<int> first;
  box<char> second;
  second.value = 0;
  int one = inspect(first);
  int two = inspect(first);
  int ordinary = shadowed(0);
  return one == sizeof(int) && two == sizeof(int) &&
    second.value == 0 && ordinary == 0 ? 0 : 1;
}
