#pragma once

#include <cstddef>

namespace cppgm
{

class ScopedCounterIncrement
{
public:
	explicit ScopedCounterIncrement(std::size_t* counter,
		bool active = true)
		: counter_(counter), active_(active)
	{
		if (active_) ++*counter_;
	}
	~ScopedCounterIncrement()
	{
		if (active_) --*counter_;
	}

private:
	ScopedCounterIncrement(const ScopedCounterIncrement&);
	ScopedCounterIncrement& operator=(const ScopedCounterIncrement&);
	std::size_t* counter_;
	bool active_;
};

template<typename T>
class ScopedValueRestore
{
public:
	ScopedValueRestore(T* slot, const T& value)
		: slot_(slot), previous_(*slot)
	{
		*slot_ = value;
	}
	~ScopedValueRestore() { *slot_ = previous_; }

private:
	ScopedValueRestore(const ScopedValueRestore&);
	ScopedValueRestore& operator=(const ScopedValueRestore&);
	T* slot_;
	T previous_;
};

template<typename Container>
class ScopedContainerPush
{
public:
	ScopedContainerPush(Container* container,
		const typename Container::value_type& value, bool active = true)
		: container_(container), active_(active)
	{
		if (active_) container_->push_back(value);
	}
	~ScopedContainerPush()
	{
		if (active_) container_->pop_back();
	}

private:
	ScopedContainerPush(const ScopedContainerPush&);
	ScopedContainerPush& operator=(const ScopedContainerPush&);
	Container* container_;
	bool active_;
};

}
