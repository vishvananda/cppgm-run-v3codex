struct copy_defaulted_later
{
  copy_defaulted_later& operator=(const copy_defaulted_later&);
};

copy_defaulted_later& copy_defaulted_later::operator=(
    const copy_defaulted_later&) = default;

struct move_defaulted_later
{
  move_defaulted_later& operator=(move_defaulted_later&&) noexcept;
};

move_defaulted_later& move_defaulted_later::operator=(
    move_defaulted_later&&) noexcept = default;

static_assert(!__is_trivially_assignable(
                  copy_defaulted_later&, const copy_defaulted_later&),
              "a special member defaulted after its first declaration is user-provided");
static_assert(!__is_nothrow_assignable(
                  copy_defaulted_later&, const copy_defaulted_later&),
              "a potentially-throwing first declaration remains potentially throwing");
static_assert(!__is_trivially_assignable(
                  move_defaulted_later&, move_defaulted_later&&),
              "an explicitly noexcept defaulted-later move is still non-trivial");
static_assert(__is_nothrow_assignable(
                  move_defaulted_later&, move_defaulted_later&&),
              "an explicit noexcept specification remains authoritative");

int main()
{
  return 0;
}
