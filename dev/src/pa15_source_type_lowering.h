#pragma once

#include "semantic/model/graph.h"
#include "pa15_lowir_types.h"

namespace cppgm
{
namespace pa15_lowering_detail
{

class SourceTypeLowering
{
public:
	explicit SourceTypeLowering(const pa12_semantic_detail::Program& program);

	pa15_lowir_detail::LowType Lower(pa11::TypeId type) const;
	pa15_lowir_detail::LowType LowerExpression(pa11::TypeId type) const;
	pa15_lowir_detail::LowType LowerStorage(pa11::TypeId type) const;
	bool IsReference(pa11::TypeId type) const;
	bool IsArray(pa11::TypeId type) const;
	bool IsFunction(pa11::TypeId type) const;
	bool IsClassObject(pa11::TypeId type) const;
	bool IsComplexObject(pa11::TypeId type) const;
	bool IsPointerLike(pa11::TypeId type) const;
	bool IsNullptr(pa11::TypeId type) const;
	pa11::TypeId RemoveReference(pa11::TypeId type) const;
	pa11::TypeId RemoveTopQualifiers(pa11::TypeId type) const;
	pa11::TypeId ExpressionObject(pa11::TypeId type) const;
	pa11::TypeId ArrayElement(pa11::TypeId type) const;
	pa11::TypeId Pointee(pa11::TypeId type) const;

private:
	const pa12_semantic_detail::Program& program_;
};

}
}
