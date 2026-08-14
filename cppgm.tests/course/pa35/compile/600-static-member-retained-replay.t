template<class Owner>
struct replay
{
  static int select(int value) { return value + 1; }
  static long select(long value) { return value + 2; }

  template<class Value>
  static Value invoke(Value value)
  {
    return select(value);
  }
};

int retained_static_member_anchor()
{
  int first = replay<int>::invoke(1);
  long second = replay<long>::invoke(2L);
  return first + static_cast<int>(second);
}
