#pragma once

template<class... Types>
int pack_sum(Types&&... values)
{
  return sizeof...(values);
}

extern template int pack_sum<int&>(int&);

extern "C" int host_pack_sum();
