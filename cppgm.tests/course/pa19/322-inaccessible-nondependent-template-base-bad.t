// An unused class template with an inaccessible nondependent base is rejected.

class access_owner
{
  class hidden
  {
  };
};

template<class T>
class invalid_derived : access_owner::hidden
{
};

int main()
{
  return 0;
}
