// AUDIT: static members still participate in the mixed ref-qualifier rule.

struct MixedStaticRef
{
  static void select();
  void select() &;
};

int main()
{
  return 0;
}
