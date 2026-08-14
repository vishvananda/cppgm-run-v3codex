template<class Owner>
struct replay
{
  static int select(int*);

  template<class Value>
  static int invoke(Value* value)
  {
    return select(value);
  }
};

long retained_no_viable_value;

int retained_no_viable_anchor()
{
  return replay<int>::invoke(&retained_no_viable_value);
}
