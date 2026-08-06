// AUDIT: aggregate union initialization selects one active member.
union YValue
{
  int selected;
  int overwritten;
};

int main()
{
  YValue value = { 7 };
  return value.selected != 7;
}
