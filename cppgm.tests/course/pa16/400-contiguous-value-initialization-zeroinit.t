struct YValueBase
{
  char first;
  long second;
};

struct YValue : YValueBase
{
  YValue() : YValueBase() {}
};

int main()
{
  YValue value{};
  return value.first == 0 && value.second == 0 ? 0 : 1;
}
