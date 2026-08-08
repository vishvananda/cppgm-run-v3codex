// AUDIT: constructor list-initialization rejects narrowing even when a
// synthesized copy candidate could otherwise rebuild the same class.

struct narrowed
{
  narrowed(int);
};

int main()
{
  narrowed value{1.5};
}
