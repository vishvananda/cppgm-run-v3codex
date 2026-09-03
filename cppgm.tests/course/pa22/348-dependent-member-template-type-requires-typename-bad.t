// A dependent member template-id used as a type requires typename as well as template.

template<class T>
struct owner
{
  typedef T::template rebind<int> invalid_type;
};

int main()
{
  return 0;
}
