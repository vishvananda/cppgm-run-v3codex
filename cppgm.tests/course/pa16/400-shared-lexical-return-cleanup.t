struct Trace
{
  int * value;
  Trace(int * p) : value(p) {}
  ~Trace() { *value = *value * 10 + 1; }
};

int choose(bool first, int * trace)
{
  Trace outer(trace);
  Trace inner(trace);
  if(first) return 7;
  return 9;
}

int main()
{
  int first_trace = 0;
  int second_trace = 0;
  return choose(true, &first_trace) != 7 || first_trace != 11 ||
    choose(false, &second_trace) != 9 || second_trace != 11;
}
