class Container
{
public:
	void assign()
	{
		Slot& slot = slots_[0];
		slot.value = 1;
	}

private:
	struct Slot
	{
		int value;
	};

	Slot slots_[1];
};
