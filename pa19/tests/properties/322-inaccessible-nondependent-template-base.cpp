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
