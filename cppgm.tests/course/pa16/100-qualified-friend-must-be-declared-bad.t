// AUDIT: a qualified friend declaration must name a previously declared function.

namespace YQualifiedFriend {
struct Owner {
  friend void YQualifiedFriend::set(Owner &);
};
}

int main() {
  return 0;
}
