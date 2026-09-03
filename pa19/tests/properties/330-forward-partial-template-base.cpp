template<class>
struct selected_base
{
};

template<class T>
struct selected_base<T*>;

template<class T>
struct invalid_derived : selected_base<int*>
{
};

int main()
{
  return 0;
}
