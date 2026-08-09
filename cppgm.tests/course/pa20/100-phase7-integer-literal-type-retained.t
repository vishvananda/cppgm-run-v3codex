// Phase 7 has already selected each literal's first representable C++11 type.
// PA20 constant evaluation must consume that typed fact instead of inferring
// every unsuffixed literal to be int from its spelling.
static_assert(0xffffffff > 0, "hex literal selects unsigned int");
static_assert(0x80000000 > 0, "high-bit hex literal remains unsigned");
static_assert(2147483648 > 0, "decimal literal selects long");
static_assert(4294967295 > 0, "large decimal literal remains signed long");
static_assert(!(-1LL < 1UL),
              "mixed long-long/unsigned-long converts to unsigned long long");
static_assert((true ? -1LL : 1UL) > 1UL,
              "conditional expression retains the common unsigned type");
static_assert(!(false && (1 / 0)),
              "logical and does not fold its discarded operand");
static_assert(true || (1 / 0),
              "logical or does not fold its discarded operand");
static_assert((true ? 1 : 1 / 0) == 1,
              "conditional does not fold its discarded arm");
static_assert((false ? 1 / 0 : 1) == 1,
              "conditional selects the false arm without folding the true arm");
static_assert(L'\u00e9' == 0xe9,
              "character literal consumes the retained decoded code unit");
