struct result
{
  long first;
  long second;
};

result fail_result()
{
  throw 1;
}
