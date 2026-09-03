// An unused class template deriving from a final class is rejected.

struct closed_base final
{
};

template<class T>
struct invalid_derived : closed_base
{
};

int main()
{
  return 0;
}
