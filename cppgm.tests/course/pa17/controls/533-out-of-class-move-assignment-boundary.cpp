struct MoveBox
{
	int value;

	MoveBox(int initial): value(initial) {}
	MoveBox& operator=(MoveBox&& other);
};

MoveBox& MoveBox::operator=(MoveBox&& other) = default;

int main()
{
	MoveBox source(9);
	MoveBox destination(1);
	destination = static_cast<MoveBox&&>(source);
	return destination.value == 9 ? 0 : 1;
}
