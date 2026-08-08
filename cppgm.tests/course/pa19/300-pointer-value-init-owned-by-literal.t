// AUDIT: pointer value-initialization is a typed literal fact in every use
// context, not a call-argument lowering exception.

typedef void* pointer;

pointer make_pointer()
{
  return pointer();
}

int main()
{
  pointer value = pointer();
  return make_pointer() == value ? 0 : 1;
}
