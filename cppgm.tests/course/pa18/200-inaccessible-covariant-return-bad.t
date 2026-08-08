struct BaseReturn
{
};

struct DerivedReturn : private BaseReturn
{
};

struct BaseOwner
{
	virtual BaseReturn* clone();
};

struct DerivedOwner : BaseOwner
{
	DerivedReturn* clone() override;
};
