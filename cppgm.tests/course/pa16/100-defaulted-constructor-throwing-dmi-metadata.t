// AUDIT: generated constructor metadata is conservative for a throwing DMI.

int may_throw();
struct Y {
  int value = may_throw();
  Y() = default;
};

int main() {
  Y object;
  return 0;
}
