template<class T>
struct owner
{
  typedef int value_type;
  static value_type get();
};

template<class T>
owner<T>::value_type owner<T>::get()
{
  return 0;
}

int main()
{
  return 0;
}
