// VALIDATION: an empty parenthesized array mem-initializer value-initializes
// every class element.

struct element
{
  element() {}
};

struct owner
{
  element values[2];

  owner() : values() {}
};
