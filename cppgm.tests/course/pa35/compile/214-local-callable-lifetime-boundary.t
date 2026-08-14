void may_throw();
void consume(int);

struct guard
{
  ~guard();
};

void local_callable_lifetime_boundary()
{
  guard enclosing;
  int value = 1;

  struct local
  {
    int value;

    local() : value(2) {}
    ~local() { may_throw(); }
    int read() const { return value; }
  };

  local owned;
  consume(owned.read());
  consume(value);
}
