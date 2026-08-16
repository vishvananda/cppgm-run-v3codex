long double signed_to_long_double(__int128_t value)
{
  return static_cast<long double>(value);
}

long double unsigned_to_long_double(__uint128_t value)
{
  return static_cast<long double>(value);
}

double signed_to_double(__int128_t value)
{
  return static_cast<double>(value);
}

float unsigned_to_float(__uint128_t value)
{
  return static_cast<float>(value);
}

__int128_t long_double_to_signed(long double value)
{
  return static_cast<__int128_t>(value);
}

__uint128_t long_double_to_unsigned(long double value)
{
  return static_cast<__uint128_t>(value);
}

__int128_t double_to_signed(double value)
{
  return static_cast<__int128_t>(value);
}

__uint128_t float_to_unsigned(float value)
{
  return static_cast<__uint128_t>(value);
}
