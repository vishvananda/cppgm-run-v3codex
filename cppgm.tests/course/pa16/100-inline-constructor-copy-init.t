// AUDIT: inline is not the explicit constructor specifier.

struct Y {
  inline Y(int) {}
};

int main() {
  Y y = 1;
  return 0;
}
