// An unused class template deriving from a final class-template specialization is rejected.

template<class>
struct closed_base final
{
};

template<class T>
struct invalid_derived : closed_base<int>
{
};

int main()
{
  return 0;
}
