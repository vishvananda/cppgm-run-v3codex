// PA20 requires each distinct distribution across multiple template parameter
// packs to retain its own specialization identity, including when one source
// file contains more partitions than a small initial index can hold.

template<class... T>
struct types
{
};

template<class... Prefix, class... Suffix>
int partition_size(types<Prefix...>, Suffix...)
{
  return sizeof...(Prefix) + sizeof...(Suffix);
}

int main()
{
  int total = 0;
  total += partition_size(types<>(), 0);
  total += partition_size(types<int>(), 0);
  total += partition_size(types<int, int>(), 0);
  total += partition_size(types<int, int, int>(), 0);
  total += partition_size(types<int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int, int, int,
                                int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int, int, int,
                                int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int, int, int,
                                int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int, int, int,
                                int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int, int, int,
                                int, int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int>(), 0);
  total += partition_size(types<int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int, int, int,
                                int, int, int, int, int, int, int>(), 0);
  return total == 300 ? 0 : 1;
}
