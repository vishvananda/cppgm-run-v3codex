[[noreturn]] void standard_stop();
void gnu_stop() __attribute__((__noreturn__));

int constant_loop(bool stop)
{
  while (true)
    if (stop) return 3;
}

int standard_switch(int value)
{
  switch (value)
  {
  case 0: return 4;
  default: standard_stop();
  }
}

int gnu_fallthrough(bool ready)
{
  if (ready) return 5;
  gnu_stop();
}

int main()
{
  return constant_loop(true) + standard_switch(0) +
    gnu_fallthrough(true) - 12;
}
