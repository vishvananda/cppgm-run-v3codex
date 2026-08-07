// AUDIT: a user-defined string literal is not a character-array initializer.
int operator "" _tag(const char*, unsigned long)
{
  return 1;
}

struct YText
{
  char bytes[2];
};

YText text = {"a"_tag};
int main() { return 0; }
