// AUDIT: an uninitialized const scalar member deletes the implicit default constructor.

struct YConstMember {
  const int value;
};

int main() {
  YConstMember object;
  return sizeof(object);
}
