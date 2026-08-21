static int leaf(int value)
{
	return value + 1;
}

static int wrapper(int value)
{
	return leaf(value) + 1;
}

int main()
{
	return wrapper(40) == 42 ? 0 : 1;
}
