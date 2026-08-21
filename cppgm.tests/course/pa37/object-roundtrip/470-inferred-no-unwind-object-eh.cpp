struct guard
{
	~guard();
};

int may_throw(int);

inline int identity(int value)
{
	return value;
}

int call_with_cleanup(int value)
{
	guard active;
	return may_throw(identity(value));
}
