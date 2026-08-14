int maybe_value(bool enabled)
{
  if (enabled) return 1;
}

int main()
{
  return maybe_value(true) - 1;
}
