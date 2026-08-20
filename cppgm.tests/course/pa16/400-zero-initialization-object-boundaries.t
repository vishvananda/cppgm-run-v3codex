struct YVolatileBase
{
  volatile long first;
  long second;
};

struct YVolatileValue : YVolatileBase
{
  YVolatileValue() : YVolatileBase() {}
};

union YChoice
{
  long first;
  char bytes[16];
};

struct YChoiceOwner
{
  YChoice choice;
  YChoiceOwner() : choice() {}
};

int main()
{
  YVolatileValue volatile_value;
  YChoiceOwner union_value;
  return volatile_value.first == 0 && volatile_value.second == 0 &&
         union_value.choice.first == 0 ? 0 : 1;
}
