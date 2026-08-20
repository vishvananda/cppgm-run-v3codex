template<int Index>
int read_digits()
{
  constexpr char first[] = "01" "23" "45";
  constexpr char second[] = "01" "23" "45";
  return first != second && first[Index] == second[Index] ? first[Index] : 0;
}

int main()
{
  return read_digits<2>() != '2' || read_digits<5>() != '5';
}
