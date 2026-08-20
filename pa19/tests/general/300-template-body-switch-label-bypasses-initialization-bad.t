template<class T>
struct switch_condition_body {
  int run() {
    switch(int tag = 0) {
    case 0:
      int value = tag;
      return value;
    default:
      return 1;
    }
  }
};

int main() {
  return 0;
}
