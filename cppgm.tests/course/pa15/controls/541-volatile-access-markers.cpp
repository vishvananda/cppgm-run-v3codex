struct Device
{
  volatile int status;
  int data;
};

int local_access()
{
  volatile int value = 1;
  value = 2;
  return value;
}

int pointer_access(volatile int * value)
{
  return *value;
}

int member_access(Device & device)
{
  int observed = device.status;
  device.status = observed + 1;
  device.data = observed + 2;
  return observed;
}

int main()
{
  Device device = {3, 0};
  volatile int pointed = 4;
  return local_access() + pointer_access(&pointed) + member_access(device) == 9
    ? 0 : 1;
}
