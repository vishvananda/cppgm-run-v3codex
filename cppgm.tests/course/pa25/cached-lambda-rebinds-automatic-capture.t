unsigned int replay(const unsigned int& value)
{
  auto add = [&](unsigned int increment) {
    return value + increment;
  };
  return add(1u);
}
