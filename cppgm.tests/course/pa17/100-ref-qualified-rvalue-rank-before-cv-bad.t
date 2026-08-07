// AUDIT: rvalue-reference binding wins before cv-subset ranking.

struct RefCvRank
{
public:
  int select() const & { return 1; }

private:
  int select() const volatile && { return 2; }
};

int main()
{
  return RefCvRank().select();
}
