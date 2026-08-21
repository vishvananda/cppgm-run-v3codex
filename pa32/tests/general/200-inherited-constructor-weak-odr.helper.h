struct inherited_linkage_base
{
  int value;

  inherited_linkage_base(int incoming) : value(incoming) {}
};

struct inherited_linkage_derived : inherited_linkage_base
{
  using inherited_linkage_base::inherited_linkage_base;

  __attribute__((noinline))
  inherited_linkage_derived(long incoming)
    : inherited_linkage_base(incoming) {}
};
