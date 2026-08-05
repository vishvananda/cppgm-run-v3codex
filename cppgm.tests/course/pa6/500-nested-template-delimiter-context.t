int main()
{
	TC<(T < 2)> relational;
	TC<(T < 1) + (2 > 1)> sibling_regions;
	TC<[=]{ return T < 2; }> lambda;
	TC<(T<1>)> nested_template;
}
