namespace std {
class type_info {
public:
  bool operator==(const type_info &) const;
};
}

struct owner { int member; };
struct polymorphic { virtual int key() { return 0; } };
typedef int function_type();
typedef int array_type[3];
typedef int owner::* member_pointer_type;

int probe(polymorphic &value)
{
  return typeid(function_type) == typeid(function_type) &&
    typeid(array_type) == typeid(array_type) &&
    typeid(member_pointer_type) == typeid(member_pointer_type) &&
    typeid(typeid(value)) == typeid(typeid(value));
}
