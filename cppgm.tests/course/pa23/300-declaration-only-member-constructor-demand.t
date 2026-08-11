// A declaration-only member constructor is not an empty definition.  The
// implicit owner's initialization must retain the member call and emission
// demand instead of speculatively eliding the complete constructor chain.

struct member {
  member();
};

struct owner {
  member value;
};

int make_owner() {
  owner value;
  return 0;
}
