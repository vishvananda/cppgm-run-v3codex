// N3485 14.7.2 [temp.explicit] p3: an unqualified-id belongs in the template namespace.

namespace owner
{
  template<class T>
  struct box
  {
    int value() { return 0; }
  };
}

using owner::box;
template class box<int>;

int main()
{
  return 0;
}
