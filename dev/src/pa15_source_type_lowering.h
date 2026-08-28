#pragma once

#include "semantic/model/graph.h"
#include "lowering/ir/types.h"

namespace cppgm
{
namespace pa15_lowering_detail
{

class SourceTypeLowering
{
public:
	explicit SourceTypeLowering(const semantic::Program& program);

	pa15_lowir_detail::LowType Lower(semantic::TypeId type) const;
	pa15_lowir_detail::LowType LowerExpression(semantic::TypeId type) const;
	pa15_lowir_detail::LowType LowerStorage(semantic::TypeId type) const;
	bool IsReference(semantic::TypeId type) const;
	bool IsArray(semantic::TypeId type) const;
	bool IsFunction(semantic::TypeId type) const;
	bool IsClassObject(semantic::TypeId type) const;
	bool IsComplexObject(semantic::TypeId type) const;
	bool IsPointerLike(semantic::TypeId type) const;
	bool IsNullptr(semantic::TypeId type) const;
	semantic::TypeId RemoveReference(semantic::TypeId type) const;
	semantic::TypeId RemoveTopQualifiers(semantic::TypeId type) const;
	semantic::TypeId ExpressionObject(semantic::TypeId type) const;
	semantic::TypeId ArrayElement(semantic::TypeId type) const;
	semantic::TypeId Pointee(semantic::TypeId type) const;

private:
	const semantic::Program& program_;
};

}
}
