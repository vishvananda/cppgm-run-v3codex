class owner
{
	int value(int input) const;

public:
	int call() const;
};

int owner::value(int input) const
{
	return input;
}

int owner::call() const
{
	auto invoke = [&](int input) { return value(input); };
	return invoke(7);
}

int use()
{
	owner object;
	return object.call();
}
