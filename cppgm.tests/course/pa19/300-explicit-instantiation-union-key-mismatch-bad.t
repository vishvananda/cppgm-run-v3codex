// N3485 14.7.2 [temp.explicit] p3: the elaborated-type-specifier must agree.

template<class T>
union box
{
  T value;
};

template class box<int>;

int main()
{
  return 0;
}
