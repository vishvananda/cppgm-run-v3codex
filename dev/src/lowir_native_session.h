#pragma once

#include "lowir_model.h"
#include "lowir_native_abi.h"
#include "mir_model.h"

#include <memory>
#include <vector>

namespace lowir_native {
struct Stats;
namespace session_detail {

class LiteralIdentityMap
{
public:
  explicit LiteralIdentityMap(const lowir_model::StringPool & source);

  lowir_model::StringId map(lowir_model::StringId source_literal);
  std::shared_ptr<std::vector<std::string> > spellings() const;

private:
  const lowir_model::StringPool & source_;
  std::vector<lowir_model::StringId> mapped_;
  std::shared_ptr<std::vector<std::string> > spellings_;
};

mir_model::MirFunction lower_native_function(
    const lowir_model::LowirProgram & program,
    const lowir_model::LowirFunction & function,
    const std::vector<unsigned char> & pointer_globals,
    const std::vector<lowir_model::SymbolId> & tls_wrappers,
    const abi::FunctionSignatureIndex & signatures,
    LiteralIdentityMap & literals,
    Stats * stats);

}  // namespace session_detail
}  // namespace lowir_native
