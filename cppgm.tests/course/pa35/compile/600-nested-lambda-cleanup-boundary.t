struct token
{
  token();
  token(const token&);
  ~token();
};

token relay(const token&);

int nested_lambda_cleanup_anchor()
{
  token outer;
  auto nested = []() -> token
  {
    return relay(token());
  };
  token result = nested();
  return 0;
}
