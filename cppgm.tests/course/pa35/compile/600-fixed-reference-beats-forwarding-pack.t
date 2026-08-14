template<class T>
struct holder
{
  template<bool Enabled = true>
  holder(const T&) {}

  template<class... U>
  holder(U&&...) {}
};

int value;
holder<const int&> selected(value);
