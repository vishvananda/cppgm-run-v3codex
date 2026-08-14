template<unsigned Index, class... Elements>
struct indexed_result
{
  static_assert(Index < sizeof...(Elements), "index must be in range");
  typedef int type;
};

template<class... Elements>
struct tuple_like {};

template<unsigned Index, class... Elements>
typename indexed_result<Index, Elements...>::type
select(tuple_like<Elements...>&);

int use()
{
  tuple_like<int> value;
  return select<1>(value);
}
