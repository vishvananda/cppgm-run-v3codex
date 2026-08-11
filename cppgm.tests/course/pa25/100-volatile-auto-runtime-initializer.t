constexpr int next_value() {
  return 7;
}

int main() {
  volatile auto value = next_value();
  return value - 7;
}
