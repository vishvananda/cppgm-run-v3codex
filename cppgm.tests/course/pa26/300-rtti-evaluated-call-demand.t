namespace std {
class type_info {
public:
  bool operator==(const type_info &) const;
};
}

struct base {
  virtual int key() { return 0; }
};

struct argument {
  argument() {}
};

inline base &select(argument, base &value) { return value; }

int probe(base &value)
{
  return typeid(select(argument(), value)) == typeid(base);
}
