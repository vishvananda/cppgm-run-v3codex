template<class T>
struct external_value
{
  int value;
  external_value& operator=(const external_value&) = default;
};

extern template struct external_value<char>;

void copy_value(external_value<char>& target,
                const external_value<char>& source)
{
  target = source;
}

template<class T>
T identity(T value)
{
  return value;
}

int main()
{
  external_value<char> first;
  external_value<char> second;
  first.value = 0;
  second.value = 9;
  copy_value(first, second);
  return first.value == 9 && identity(4) == 4 ? 0 : 1;
}
