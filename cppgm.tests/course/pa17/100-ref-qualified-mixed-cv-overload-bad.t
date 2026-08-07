// AUDIT: the mixed ref-qualifier rule ignores member cv-qualification.

struct MixedRefCv
{
  void select() const;
  void select() volatile &;
};

int main()
{
  return 0;
}
