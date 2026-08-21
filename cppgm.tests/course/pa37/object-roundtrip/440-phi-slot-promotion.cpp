__attribute__((noinline)) long long advance(bool choose_side)
{
	long long value = 7;
	if(choose_side)
		value = 9;
	while(value < 12)
		++value;
	return value;
}

int main()
{
	return advance(false);
}
