bool choose_left;

struct Value
{
  long payload;

  explicit Value(long value) : payload(value) {}
  Value(const Value & other) : payload(other.payload) {}
  Value(Value && other) : payload(other.payload) { other.payload = -1; }
  ~Value() {}
};

long select_value()
{
  Value result = choose_left ? Value(17) : Value(23);
  return result.payload;
}
