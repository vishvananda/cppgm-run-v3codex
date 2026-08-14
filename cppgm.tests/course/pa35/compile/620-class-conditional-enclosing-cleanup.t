int alive;
int observed;
bool choose_second = true;

struct value {
  explicit value(int) { ++alive; }
  value(const value&) { ++alive; }
  ~value() { --alive; }
};

void consume(const value&, const value&)
{
  observed = alive;
}

int main()
{
  consume(value(0), choose_second ? value(1) : value(2));
  return observed == 2 && alive == 0 ? 0 : 1;
}
