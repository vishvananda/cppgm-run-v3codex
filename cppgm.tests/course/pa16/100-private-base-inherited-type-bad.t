// AUDIT: a public type inherited through a private base is private in the derived class.

struct YTypeBase {
  typedef int value_type;
};

class YTypeDerived : YTypeBase {};

YTypeDerived::value_type value;
