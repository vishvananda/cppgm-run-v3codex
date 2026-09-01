#if !__has_attribute(cppgm_stable_prefix)
#error cppgm_stable_prefix must be available during object preparation
#endif

static int values[3] = {5, 7, 11};

extern "C" __attribute__((cppgm_stable_prefix, noinline))
int stable_prefix_query(const int* state, unsigned index_value)
{
  return state[index_value];
}

int main()
{
  int first = stable_prefix_query(values, 0);
  int higher = stable_prefix_query(values, 2);
  int again = stable_prefix_query(values, 0);
  return first + higher + again != 21;
}
