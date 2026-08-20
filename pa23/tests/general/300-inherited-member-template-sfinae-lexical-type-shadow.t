namespace lib {
inline namespace v1 {

template<bool B, class T = void>
struct enable_if {};

template<class T>
struct enable_if<true, T> {
  typedef T type;
};

template<bool B, class T = void>
using __enable_if_t = typename enable_if<B, T>::type;

template<class A, class B>
struct is_same {
  static const bool value = false;
};

template<class T>
struct is_same<T, T> {
  static const bool value = true;
};

template<class Key, class Value>
struct pair {
  Key first;
  Value second;
};

template<class Char>
struct char_traits {};

template<class T>
struct allocator {};

template<class Char, class Traits, class Alloc>
struct basic_string {};

template<class Key, class Value>
struct node_value {
  typedef pair<const Key, Value> container_value_type;

  container_value_type value;

  const container_value_type & __get_value() const {
    return value;
  }
};

template<class T>
struct key_value_types;

template<class Key, class Value>
struct key_value_types<node_value<Key, Value> > {
  typedef node_value<Key, Value> node_value_type;
  typedef pair<const Key, Value> container_value_type;

  template<class Up,
           __enable_if_t<is_same<Up, node_value_type>::value,
                         int> = 0>
  static const container_value_type & __get_value(Up & value) {
    return value.__get_value();
  }

  template<class Up,
           __enable_if_t<is_same<Up, container_value_type>::value,
                         int> = 0>
  static const container_value_type & __get_value(Up & value) {
    return value;
  }
};

template<class Node>
struct node_types : key_value_types<Node> {
  typedef typename key_value_types<Node>::container_value_type node_value_type;
};

template<class node_value_type, class NodeTypes>
struct table {
  node_value_type assign(node_value_type & value) {
    return NodeTypes::__get_value(value);
  }
};

typedef basic_string<char, char_traits<char>, allocator<char> > string_type;
typedef node_value<string_type, unsigned long> node_type;
typedef node_types<node_type> traits;
typedef pair<const string_type, unsigned long> container_type;

}
}

int main() {
  using namespace lib;
  node_type node = {{{}, 1}};
  container_type value = {{}, 2};
  table<container_type, traits> values;
  return traits::__get_value(node).second == 1 &&
      values.assign(value).second == 2 ? 0 : 1;
}
