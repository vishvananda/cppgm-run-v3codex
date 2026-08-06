// AUDIT: a user-provided constructor must initialize each reference member.

struct YReference {
  int& value;
  YReference() {}
};

int main() {
  YReference object;
  return 0;
}
