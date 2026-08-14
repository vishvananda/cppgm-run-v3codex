static_assert((~0u << 1) == 0xfffffffeu,
              "unsigned int left shift reduces modulo its width");
static_assert((~0ull << 4) == 0xfffffffffffffff0ull,
              "unsigned long long left shift reduces modulo its width");

using uint128 = __uint128_t;
static_assert((~uint128(0) << 4) == (~uint128(0) ^ uint128(15)),
              "unsigned 128-bit left shift uses the same modulo rule");

int main()
{
  return 0;
}
