// AUDIT: nested class arrays construct forward and destroy in reverse order.

struct Y {
  Y() noexcept {}
  ~Y() noexcept {}
};

int main() {
  Y values[2][2];
  return 0;
}
