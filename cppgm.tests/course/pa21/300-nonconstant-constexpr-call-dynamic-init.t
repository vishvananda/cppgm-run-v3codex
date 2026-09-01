// N3485 focus: 3.6.2 [basic.start.init] permits dynamic initialization when
// an ordinary namespace variable's initializer is not a constant expression.
constexpr int divide(int numerator, int denominator)
{
  return numerator / denominator;
}

int runtime_value = divide(10, 0);

int main() { return 0; }
