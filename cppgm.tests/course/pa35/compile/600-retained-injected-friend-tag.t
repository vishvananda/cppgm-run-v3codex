template<class T>
struct owner
{
  template<class U> friend struct owner;
  owner();
};

template<class T>
owner<T>::owner() {}

owner<int> value;
