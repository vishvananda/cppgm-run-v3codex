// AUDIT: a trivial defaulted destructor is still subject to access control.

struct Y {
private:
  ~Y() = default;
public:
  Y() = default;
};

int main() {
  Y value;
  return 0;
}
