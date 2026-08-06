// AUDIT: extern and thread_local are independent declaration facts.

extern thread_local int value;
int *address = &value;

int main()
{
  return 0;
}
