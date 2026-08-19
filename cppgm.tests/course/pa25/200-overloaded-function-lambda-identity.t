// Lambdas in overloads with the same terminal name and lambda ordinal still
// have distinct closure and call-operator identities.
int select(int)
{
	auto value = []() { return 1; };
	return value();
}

int select(char)
{
	auto value = []() { return 2; };
	return value();
}

int main()
{
	return select(0) + select('x') == 3 ? 0 : 1;
}
