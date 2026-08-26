const char * literal_pointer()
{
  return "student-visible literal storage";
}

char writable_bytes[] = {'o', 'k', 0};

char * writable_pointer()
{
  return writable_bytes;
}

int main()
{
  return literal_pointer()[0] == 's' && writable_pointer()[0] == 'o' ? 0 : 1;
}
