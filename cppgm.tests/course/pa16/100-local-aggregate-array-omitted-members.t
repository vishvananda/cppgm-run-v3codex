// AUDIT: omitted members in each local aggregate-array element are value-initialized.
struct YPair
{
  int first;
  int second;
};

int main()
{
  YPair values[2] = {{}, {1}};
  return values[0].first + values[0].second + values[1].second;
}
