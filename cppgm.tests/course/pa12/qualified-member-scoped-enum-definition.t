struct writer
{
  enum class state : int;
};

enum class writer::state : int
{
  ready = 1
};

int value()
{
  return static_cast<int>(writer::state::ready);
}
