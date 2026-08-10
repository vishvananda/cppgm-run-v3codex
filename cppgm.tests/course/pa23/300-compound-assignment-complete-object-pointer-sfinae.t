template<class T> T&& value();

template<class T, class U,
  class = decltype(value<T>() += value<U>())>
char probe(int);

template<class, class>
int probe(...);

struct Incomplete;
using Function = int();
using CompleteArray = int[3];
using UnknownArray = int[];

static_assert(sizeof(probe<bool&, int*>(0)) == sizeof(char), "");
static_assert(sizeof(probe<bool&, CompleteArray*>(0)) == sizeof(char), "");
static_assert(sizeof(probe<bool&, void*>(0)) == sizeof(int), "");
static_assert(sizeof(probe<bool&, Function*>(0)) == sizeof(int), "");
static_assert(sizeof(probe<bool&, Incomplete*>(0)) == sizeof(int), "");
static_assert(sizeof(probe<bool&, UnknownArray*>(0)) == sizeof(int), "");

int main() { return 0; }
