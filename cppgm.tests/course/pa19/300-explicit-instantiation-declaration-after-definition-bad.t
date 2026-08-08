// N3485 14.7.2 [temp.explicit] p11: a declaration cannot follow the definition.

template<class T>
struct box
{
  int value() { return 0; }
};

template class box<int>;
extern template class box<int>;

int main()
{
  return 0;
}
