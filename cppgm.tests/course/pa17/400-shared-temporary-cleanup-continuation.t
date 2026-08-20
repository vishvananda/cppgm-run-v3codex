int trace;

struct Value
{
  int number;
  Value(int n) : number(n) {}
  ~Value() { trace = trace * 10 + number; }
};

int read(Value const & value)
{
  return value.number;
}

int choose(bool first)
{
  if(first) return read(Value(7));
  return read(Value(9));
}

int main()
{
	trace = 0;
	if(choose(true) != 7 || trace != 7) return 1;
	trace = 0;
	return choose(false) != 9 || trace != 9;
}
