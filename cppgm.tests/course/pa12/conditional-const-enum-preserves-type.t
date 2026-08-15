enum choice
{
  none,
  some
};

void accept(choice);

void select(bool condition)
{
  const choice selected = some;
  accept(condition ? selected : none);
}
