extern "C" long select_address(bool choose)
{
	long values[1];
	if (choose) {
		long *address = values;
		*address = 11;
	} else {
		long *address = values;
		*address = 22;
	}
	long *address = values;
	return *address;
}

int main()
{
	return select_address(true) == 11 && select_address(false) == 22
		? 0 : 1;
}
