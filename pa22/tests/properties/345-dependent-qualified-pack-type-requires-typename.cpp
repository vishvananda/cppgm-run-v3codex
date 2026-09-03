template<unsigned long Value>
struct selected
{
  typedef int type;
};

template<class... Types>
struct owner
{
  typedef selected<sizeof...(Types)>::type invalid_type;
};

int main()
{
  return 0;
}
