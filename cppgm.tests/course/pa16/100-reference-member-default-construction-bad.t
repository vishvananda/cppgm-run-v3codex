// AUDIT: an uninitialized reference member deletes the implicit default constructor.

struct YReferenceMember {
  int& value;
};

int main() {
  YReferenceMember object;
  return sizeof(object);
}
