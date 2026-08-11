int main() {
  int value = 1;
  const auto&& reference = value;
  return reference;
}
