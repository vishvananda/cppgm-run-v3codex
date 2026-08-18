#pragma once

// Shared symbol facts carried by LowIR and MIR model scaffolds.
//
// This is intentionally smaller than the semantic-layer symbol representation:
// by the time a program crosses the LowIR boundary, backend code should only
// need concrete internal/object spellings and linkage choices.

namespace ir_model {

enum SymbolLinkage
{
  SL_INTERNAL,
  SL_EXTERNAL,
  SL_WEAK
};

}  // namespace ir_model
