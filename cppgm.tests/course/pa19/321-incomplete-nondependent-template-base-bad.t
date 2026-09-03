// An unused class template with an incomplete nondependent base is rejected.

namespace external
{
template<bool>
struct invalid_derived;
}

template<class T>
struct invalid_derived : external::invalid_derived<false>
{
};

int main()
{
  return 0;
}
