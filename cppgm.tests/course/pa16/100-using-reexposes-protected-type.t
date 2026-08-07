// AUDIT: a public using-declaration re-exposes a protected member type.

struct YUsingTypeBase {
protected:
  typedef int value_type;
};

struct YUsingTypeDerived : YUsingTypeBase {
  using YUsingTypeBase::value_type;
};

YUsingTypeDerived::value_type value;

int main() {
  return value;
}
