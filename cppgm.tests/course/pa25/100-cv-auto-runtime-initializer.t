int next_value() {
  return 7;
}

int main() {
  const auto value = next_value();
  return value - 7;
}
