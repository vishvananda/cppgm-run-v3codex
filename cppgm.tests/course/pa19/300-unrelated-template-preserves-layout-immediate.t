// AUDIT: an unrelated demanded specialization cannot change the selected
// scalar conversion policy of an ordinary expression.

template<class T>
void demand(T)
{
}

struct holder
{
  char byte;
};

int main()
{
  demand(0);
  return sizeof(holder) - 1;
}
