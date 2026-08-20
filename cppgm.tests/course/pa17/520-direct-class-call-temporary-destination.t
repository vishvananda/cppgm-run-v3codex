int trace;

struct Guard
{
  Guard() { trace = trace * 10 + 1; }
  ~Guard() { trace = trace * 10 + 3; }
};

struct Pair
{
  long first;
  long second;

  Pair(long a, long b) : first(a), second(b) {}
};

Pair make_pair(long value)
{
  return Pair(value, value + 1);
}

long read(Pair const & value, Guard const &)
{
  trace = trace * 10 + 2;
  return value.first + value.second;
}

int main()
{
  trace = 0;
  bool first = true;
  long result = first ? read(make_pair(4), Guard()) :
                        read(make_pair(7), Guard());
  return result != 9 || trace != 123;
}
