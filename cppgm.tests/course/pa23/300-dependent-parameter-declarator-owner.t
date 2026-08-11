// N3485 focus: 14.1 [temp.param], dependent non-type parameter types.

template<class T, int (*P)(T)>
struct function_holder
{
};

int source(int);
function_holder<int, &source> function_value;

int main()
{
  return 0;
}
