void invalid_cross_function_reference()
{
  int enclosing = 7;

  struct local
  {
    int read() const { return enclosing; }
  };

  local value;
  (void)value.read();
}
