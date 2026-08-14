struct first {
  typedef int value_type;
};

struct second {
  typedef int value_type;
};

struct derived : first, second {
  value_type value;
};
