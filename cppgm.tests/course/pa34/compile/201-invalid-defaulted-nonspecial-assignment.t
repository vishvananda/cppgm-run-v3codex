struct invalid_defaulted_assignment
{
  invalid_defaulted_assignment& operator=(int) = default;
};
