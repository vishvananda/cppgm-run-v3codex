// AUDIT: list-initialization ranks the element conversion, not the parameter
// against itself.

struct ranked
{
  ranked(int);
  ranked(double);
};

int use()
{
  ranked value({1});
  return 0;
}
