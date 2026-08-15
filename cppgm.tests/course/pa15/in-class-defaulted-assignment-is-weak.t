struct element
{
	element& operator=(const element&);
};

struct holder
{
	element value;
	holder& operator=(const holder&) = default;
};

holder& assign(holder& left, const holder& right)
{
	return left = right;
}
