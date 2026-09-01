#include "native/eh/references.h"
#include "native/errors.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace lowir_native {
namespace eh_reference_detail {

void emit_host_eh_reference_data(
    const lowir_model::LowirProgram & source,
    const object_elf_detail::DeclarationObjectSymbols & declarations,
    std::vector<object_elf_detail::HostFunctionLayout> & functions,
    elf_detail::CodeBuffer & data,
    std::size_t & data_alignment)
{
  bool needs_personality = false;
  std::vector<unsigned char> catch_type_seen(source.symbol_names.size(), 0);
  const auto record_eh_type = [&](lowir_model::SymbolId symbol) {
    const std::uint32_t index = symbol;
    if(!symbol.valid() || index >= catch_type_seen.size())
      native_errors::ThrowInternal("invalid EH type symbol identity");
    catch_type_seen[index] = 1;
  };
  for(std::size_t i = 0; i < functions.size(); ++i) {
    needs_personality = needs_personality || !functions[i].call_sites.empty();
    for(std::size_t block = 0; block < functions[i].clauses.size(); ++block)
      for(std::size_t clause = 0;
          clause < functions[i].clauses[block].size(); ++clause) {
        const mir_model::MirHostEhClause & item =
          functions[i].clauses[block][clause];
        if(item.kind == mir_model::MirHostEhClause::HC_CATCH &&
           !item.catch_all) {
          record_eh_type(item.type_symbol);
        } else if(item.kind == mir_model::MirHostEhClause::HC_FILTER) {
          for(std::size_t type = 0;
              type < item.filter_type_symbols.size(); ++type)
            record_eh_type(item.filter_type_symbols[type]);
        }
      }
  }

  std::vector<lowir_model::SymbolId> ordered_catch_types;
  for(std::size_t i = 0; i < catch_type_seen.size(); ++i)
    if(catch_type_seen[i])
      ordered_catch_types.push_back(
        lowir_model::SymbolId(static_cast<std::uint32_t>(i)));
  const auto catch_type_name =
    [&source, &declarations](lowir_model::SymbolId symbol)
      -> const std::string & {
      const object_elf_detail::DeclarationObjectSymbol * object =
        declarations.find(symbol);
      return object ? *object->spelling :
        lowir_model::lowir_symbol_name(source, symbol);
    };
  std::sort(ordered_catch_types.begin(), ordered_catch_types.end(),
    [&catch_type_name](lowir_model::SymbolId left,
                       lowir_model::SymbolId right) {
      return catch_type_name(left) < catch_type_name(right);
    });

  std::vector<std::pair<lowir_model::SymbolId, lowir_model::SymbolId> >
    catch_type_aliases;
  std::vector<lowir_model::SymbolId> unique_catch_types;
  unique_catch_types.reserve(ordered_catch_types.size());
  for(std::size_t i = 0; i < ordered_catch_types.size(); ++i) {
    if(!unique_catch_types.empty() &&
       catch_type_name(ordered_catch_types[i]) ==
         catch_type_name(unique_catch_types.back())) {
      catch_type_aliases.push_back(std::make_pair(
        ordered_catch_types[i], unique_catch_types.back()));
      continue;
    }
    unique_catch_types.push_back(ordered_catch_types[i]);
  }
  ordered_catch_types.swap(unique_catch_types);
  const auto canonical_catch_type = [&catch_type_aliases](
      lowir_model::SymbolId symbol) {
    for(std::size_t i = 0; i < catch_type_aliases.size(); ++i)
      if(catch_type_aliases[i].first == symbol)
        return catch_type_aliases[i].second;
    return symbol;
  };
  for(std::size_t function = 0; function < functions.size(); ++function)
    for(std::size_t block = 0;
        block < functions[function].clauses.size(); ++block)
      for(std::size_t clause = 0;
          clause < functions[function].clauses[block].size(); ++clause) {
        mir_model::MirHostEhClause & item =
          functions[function].clauses[block][clause];
        if(item.kind == mir_model::MirHostEhClause::HC_CATCH &&
           !item.catch_all) {
          item.type_symbol = canonical_catch_type(item.type_symbol);
        } else if(item.kind == mir_model::MirHostEhClause::HC_FILTER) {
          for(std::size_t type = 0;
              type < item.filter_type_symbols.size(); ++type)
            item.filter_type_symbols[type] =
              canonical_catch_type(item.filter_type_symbols[type]);
        }
      }

  for(std::size_t i = 0; i < ordered_catch_types.size(); ++i) {
    const lowir_model::SymbolId type_symbol = ordered_catch_types[i];
    data.align(8);
    data.label_eh_type_ref(type_symbol);
    data.absolute64(type_symbol);
    data_alignment = std::max<std::size_t>(data_alignment, 8);
  }
  if(needs_personality) {
    data.align(8);
    data.label_eh_personality_ref();
    data.absolute64("__gxx_personality_v0");
    data_alignment = std::max<std::size_t>(data_alignment, 8);
  }
}

}  // namespace eh_reference_detail
}  // namespace lowir_native
