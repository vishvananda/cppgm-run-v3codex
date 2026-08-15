template<typename T>
struct operation
{
  static constexpr bool nonthrowing()
  {
    return true;
  }

  void run() noexcept(nonthrowing())
  {
  }
};

bool demand_from_discarded_arm()
{
  return true || (sizeof(operation<int>), false);
}
