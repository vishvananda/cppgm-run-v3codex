template<class T>
struct Box {
  struct {
    T value;
  };

  void set(T x) { value = x; }
};

enum { box_size = sizeof(Box<int>) };

int main() { return 0; }
