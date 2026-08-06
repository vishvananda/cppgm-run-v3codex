// AUDIT: one class scope cannot own two data members with the same name.

struct YDuplicateMember {
  int value;
  int value;
};
