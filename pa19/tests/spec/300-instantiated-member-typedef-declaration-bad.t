// VALIDATION: compile-fail
// N3485 focus: 14.7.1 [temp.inst]
// Instantiating a class specialization instantiates its member declarations,
// including a typedef whose target is otherwise unused.

template<class>
struct incomplete_target;

template<class T>
struct holder
{
  typedef typename incomplete_target<T>::type type;
};

holder<int> value;

int main()
{
  return 0;
}
