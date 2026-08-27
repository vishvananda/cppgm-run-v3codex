int values[4];

int increment(int value)
{
	return value + 1;
}

int array_decay()
{
	int* pointer = values;
	return pointer[2];
}

int function_decay()
{
	int (*function)(int) = increment;
	return function(4);
}
