// AUDIT: a canonical class entity has at most one definition/layout completion.

struct YDuplicate {
  int first;
};

struct YDuplicate {
  int second;
};
