template<class T>
struct box
{
  int value;
};

template<class T>
int consume(T * value)
{
  return value->value;
}

struct owner
{
  int run()
  {
    struct local {};
    box<local> value = {0};
    return consume(&value);
  }
};

int main()
{
  owner value;
  return value.run();
}
