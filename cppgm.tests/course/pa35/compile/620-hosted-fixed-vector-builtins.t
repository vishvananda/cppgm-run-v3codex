typedef signed char v8qi __attribute__((vector_size(8)));
typedef short v4hi __attribute__((vector_size(8)));
typedef int v2si __attribute__((vector_size(8)));

v8qi make_v8qi()
{
  return __builtin_ia32_vec_init_v8qi(1, 2, 3, 4, 5, 6, 7, 8);
}

v4hi make_v4hi()
{
  return __builtin_ia32_vec_init_v4hi(10, 20, 30, 40);
}

v2si make_v2si(int low, int high)
{
  return __builtin_ia32_vec_init_v2si(low, high);
}

int extract_high(v2si value)
{
  return __builtin_ia32_vec_ext_v2si(value, 1);
}

using extracted_lane = decltype(__builtin_ia32_vec_ext_v2si(make_v2si(1, 2), 1));

static_assert(sizeof(decltype(__builtin_ia32_vec_init_v2si(1, 2))) == 8,
              "v2si constructor width");
static_assert(__is_same(extracted_lane, int), "v2si extraction type");
