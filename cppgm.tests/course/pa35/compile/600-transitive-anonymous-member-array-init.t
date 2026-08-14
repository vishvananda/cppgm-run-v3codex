struct projected_state {
  union {
    long direct;
    struct {
      long alternate;
      bool negative;
    };
  };
};

long read_projected_state(projected_state& state)
{
  return state.alternate + (state.negative ? 1 : 0);
}

struct element {
  explicit element(int);
};

struct array_owner {
  element values[2] = { element(1), element(2) };
  array_owner() {}
};

array_owner make_array_owner()
{
  return array_owner();
}
