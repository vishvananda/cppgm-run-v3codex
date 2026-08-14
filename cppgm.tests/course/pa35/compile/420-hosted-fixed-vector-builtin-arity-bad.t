typedef int v2si __attribute__((vector_size(8)));
v2si bad_vector_arity()
{
  return __builtin_ia32_vec_init_v2si(1);
}
