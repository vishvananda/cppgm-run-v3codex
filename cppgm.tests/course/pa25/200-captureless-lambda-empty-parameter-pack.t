template<class... Args>
int count_lambda_parameters()
{
  auto count = [](Args... args) { return sizeof...(args); };
  return count();
}

int main()
{
  return count_lambda_parameters<>();
}
