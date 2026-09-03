// Dependent qualified types with typename, current-instantiation members, and dependent template names are accepted.

template<unsigned long Value>
struct selected
{
  typedef int type;
};

template<class... Types>
struct dependent_owner
{
  typedef typename selected<sizeof...(Types)>::type valid_type;
};

template<class T>
struct current_owner
{
  typedef int value_type;
  current_owner<T>::value_type value;
};

template<template<class...> class Function>
struct accepts_template
{
};

template<class T>
struct template_owner
{
  template<class U>
  static void accepts(accepts_template<U::template function>*);
};

struct member_template_provider
{
  template<class>
  struct rebind
  {
    typedef int type;
  };
};

template<class T>
struct member_template_type_owner
{
  typedef typename T::template rebind<int>::type valid_type;
};

template<class T>
struct external_owner
{
  typedef int value_type;
  external_owner(const value_type&);
};

template<class T>
external_owner<T>::external_owner(
    const external_owner<T>::value_type& value)
{
  (void)value;
}

int main()
{
  dependent_owner<int, char>::valid_type first = 2;
  current_owner<int> second;
  second.value = 3;
  external_owner<int> third(first);
  member_template_type_owner<member_template_provider>::valid_type fourth = 0;
  (void)third;
  return first + second.value + fourth == 5 ? 0 : 1;
}
