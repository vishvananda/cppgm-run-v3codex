inline int add_one(int value)
{
	return value + 1;
}

inline int twice_after_add_one(int value)
{
	return add_one(value) * 2;
}

extern "C" int default_optimization_entry(int value)
{
	return twice_after_add_one(value);
}

int main()
{
	return default_optimization_entry(20) == 42 ? 0 : 1;
}
