// AUDIT: an inaccessible base destructor deletes the derived destructor.

struct YA {
private:
  ~YA() {}
};

struct YB : YA {};

int main() {
  YB value;
  return 0;
}
