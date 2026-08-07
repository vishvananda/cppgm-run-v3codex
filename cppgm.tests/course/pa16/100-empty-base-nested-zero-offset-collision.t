struct YEmpty {};

struct YMemberBase : YEmpty
{
  int value;
};

struct YMemberField
{
  YEmpty first;
};

struct YFromBase : YEmpty
{
  YMemberBase member;
};

struct YFromField : YEmpty
{
  YMemberField member;
};

int main()
{
  return sizeof(YFromBase) + sizeof(YFromField);
}
