struct C {
  void select();
  void select() const;
};

typedef void (C::* const_member)() const;

const_member take()
{
  return &C::select;
}
