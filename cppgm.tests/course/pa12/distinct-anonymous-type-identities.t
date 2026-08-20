struct { int value; } first;
struct { int value; } second;
enum { first_value = 3, second_value = 5 };

int main() {
  first.value = first_value;
  second.value = second_value;
  return first.value + second.value - 8;
}
