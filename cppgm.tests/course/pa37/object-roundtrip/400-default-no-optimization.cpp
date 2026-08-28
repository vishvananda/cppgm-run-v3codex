inline int add_one(int value)
{
	return value + 1;
}

inline int twice_after_add_one(int value)
{
	return add_one(value) * 2;
}

volatile int default_observed;

extern "C" int default_level_entry(int value)
{
	int result = twice_after_add_one(value);
	for (int i = 0; i < 4; ++i) {
		result += i;
		default_observed = result;
	}
	return result;
}

int main()
{
	return default_level_entry(20) == 48 && default_observed == 48 ? 0 : 1;
}
