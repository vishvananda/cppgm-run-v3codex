struct Device
{
  volatile int status;
  int data;
};

int poll(volatile int * port, Device & device)
{
  volatile int gate = 0;
  gate = 1;
  gate += 2;
  ++gate;
  int sampled = *port;
  sampled += device.status;
  device.status = sampled;
  device.data = sampled;
  volatile int * indirect = &gate;
  return *indirect + gate;
}

int main()
{
  Device device = {0, 0};
  int port = 5;
  return poll(const_cast<volatile int *>(&port), device) - 14;
}
