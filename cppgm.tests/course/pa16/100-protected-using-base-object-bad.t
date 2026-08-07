// AUDIT: a protected using-alias remains object-bound for derived access.

struct YProtectedUsingBase {
protected:
  int value;
};

struct YProtectedUsingMiddle : YProtectedUsingBase {
protected:
  using YProtectedUsingBase::value;
};

struct YProtectedUsingDerived : YProtectedUsingMiddle {
  int read(YProtectedUsingMiddle &object) {
    return object.value;
  }
};

int main() {
  YProtectedUsingMiddle object;
  YProtectedUsingDerived reader;
  return reader.read(object);
}
