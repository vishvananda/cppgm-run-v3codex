struct payload
{
  long first;
  long second;
};

long extended_sum(long a, long b, long c, long d, long e, long f,
                  payload tail)
{
  return a + b + c + d + e + f + tail.first + tail.second;
}

long dead_argument_pressure(long seed)
{
  long result = extended_sum(seed + 1, seed + 2, seed + 3,
                             seed + 4, seed + 5, seed + 6,
                             payload{seed + 7, seed + 8});
  return result ^ 0x55;
}

long live_argument_pressure(long seed)
{
  long a = seed + 1;
  long b = seed + 2;
  long c = seed + 3;
  long d = seed + 4;
  long e = seed + 5;
  long f = seed + 6;
  payload tail = {seed + 7, seed + 8};
  long result = extended_sum(a, b, c, d, e, f, tail);
  return result + a - b + c - d + e - f + tail.first;
}

static_assert(sizeof(&dead_argument_pressure) > 0, "dead arguments");
static_assert(sizeof(&live_argument_pressure) > 0, "live arguments");

int main()
{
  return dead_argument_pressure(3) == 105 &&
         live_argument_pressure(3) == 67 ? 0 : 1;
}
