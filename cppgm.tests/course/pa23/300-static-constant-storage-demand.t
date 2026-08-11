// N3485 focus: 3.2 [basic.def.odr], 9.4.2 [class.static.data].

template<int N>
struct values
{
  static const int plain = N;
  static constexpr int cx = N + 1;
};

template<int N>
const int values<N>::plain;

template<int N>
constexpr int values<N>::cx;

int read(const int* pointer, const int& reference)
{
  return *pointer + reference;
}

int main()
{
  int folded = values<3>::cx + values<7>::cx;
  return folded + read(&values<5>::plain, values<5>::cx) == 23 ? 0 : 1;
}
