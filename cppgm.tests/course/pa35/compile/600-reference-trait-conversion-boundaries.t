#include <type_traits>

struct class_type {};
union union_type { int value; };
enum enum_type { enum_value };

static_assert(__is_class(class_type), "class object is a class type");
static_assert(!__is_class(class_type&), "class reference is not a class type");
static_assert(__is_union(union_type), "union object is a union type");
static_assert(!__is_union(union_type&), "union reference is not a union type");
static_assert(__is_enum(enum_type), "enum object is an enum type");
static_assert(!__is_enum(enum_type&), "enum reference is not an enum type");
static_assert(__is_empty(class_type), "empty class object is empty");
static_assert(!__is_empty(class_type&), "class reference is not empty");

template<class T, class = typename std::enable_if<
  std::is_class<T>::value>::type>
char class_probe(int);

template<class>
long class_probe(...);

static_assert(sizeof(class_probe<class_type&>(0)) == sizeof(long),
              "reference trait must remove a constrained candidate");

template<class A, class B> struct same { static const bool value = false; };
template<class A> struct same<A, A> { static const bool value = true; };

struct cast_target { long value; };
alignas(cast_target) unsigned char cast_storage[sizeof(cast_target)];

using cast_lvalue = decltype(reinterpret_cast<cast_target&>(cast_storage));
using cast_xvalue = decltype(reinterpret_cast<cast_target&&>(cast_storage));
static_assert(same<cast_lvalue, cast_target&>::value,
              "reinterpret reference preserves lvalue category");
static_assert(same<cast_xvalue, cast_target&&>::value,
              "reinterpret reference preserves xvalue category");

template<class T>
struct box {
  box();
  template<class U> box(const box<U>&);
  template<class U> box(box<U>&&);
};

box<const int> convert_box(box<int>& value)
{
  return static_cast<box<int>&&>(value);
}
