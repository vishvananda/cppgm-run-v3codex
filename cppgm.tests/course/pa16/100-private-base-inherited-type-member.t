// AUDIT: a private base's public type remains accessible in the derived class.

struct YMemberTypeBase {
  typedef int value_type;
};

class YMemberTypeDerived : YMemberTypeBase {
  value_type value;
};
