struct YMember
{
  int value;
  YMember() : value(0) {}
  YMember(YMember const & other) : value(other.value) {}
};

struct YBits
{
  YMember member;
  unsigned first : 3;
  unsigned second : 5;
  unsigned : 0;
  unsigned third : 4;
  unsigned fourth : 4;
};

int main()
{
  YBits source;
  source.first = 5;
  source.second = 17;
  source.third = 9;
  source.fourth = 12;
  source.member.value = 23;
  YBits copied(source);
  return copied.first != 5 || copied.second != 17 ||
    copied.third != 9 || copied.fourth != 12 || copied.member.value != 23;
}
