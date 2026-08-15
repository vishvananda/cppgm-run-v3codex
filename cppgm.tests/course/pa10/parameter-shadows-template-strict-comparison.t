template <class T>
struct type {};

enum value_kind
{
	VALUE_FIRST,
	VALUE_LAST
};

bool outside_range(value_kind type)
{
	return type < VALUE_FIRST || type > VALUE_LAST;
}
