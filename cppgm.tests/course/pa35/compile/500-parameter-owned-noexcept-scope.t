void plain(int value) noexcept(noexcept(value + 1)) {}

struct holder {
  int member;

  void inline_set(int renamed)
    noexcept(noexcept(this->member = renamed)) {
    member = renamed;
  }

  void assign(int renamed) noexcept(noexcept(renamed + this->member));
};

struct token {
  token(int) noexcept {}
};

struct current_owner {
  int member;

  current_owner(int value) : member(value) {}
  current_owner(current_owner&& other)
    noexcept(noexcept(token(other.member))) : member(other.member) {}
};

void holder::assign(int renamed) noexcept(noexcept(renamed + this->member)) {
  member = renamed;
}

template<class T>
struct retained {
  void assign(T renamed) noexcept(noexcept(renamed + renamed));
};

int main() {
  holder value = { 0 };
  value.inline_set(3);
  value.assign(4);
  plain(value.member);
  current_owner first(5);
  current_owner second(static_cast<current_owner&&>(first));
  return value.member != 4 || second.member != 5;
}
