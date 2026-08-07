// AUDIT: derived-class access to a protected member requires a derived object.

struct YProtectedObjectBase {
protected:
  int value;
};

struct YProtectedObjectDerived : YProtectedObjectBase {
  int read(YProtectedObjectBase &object) {
    return object.value;
  }
};

int main() {
  YProtectedObjectBase object;
  YProtectedObjectDerived reader;
  return reader.read(object);
}
