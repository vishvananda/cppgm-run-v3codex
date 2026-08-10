// AUDIT: defaults added on alpha-renamed function-template redeclarations
// retain the parameter names and lexical scope of their declaring template
// head, while the eventual specialization uses one canonical argument list.

template<class First, class Second>
Second selected();

template<class A, class B = A>
B selected();

template<class X = int, class Y>
Y selected();

static_assert(sizeof(selected()) == sizeof(int),
              "merged defaults must use their declaring parameter names");

int main()
{
  return 0;
}
