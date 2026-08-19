#pragma once

// Compact storage for either textual PA14 references or resolved production
// graph handles.

#include <cstddef>
#include <string>
#include <vector>

namespace abi_mangle {

class AbiReferenceList
{
public:
  AbiReferenceList();
  AbiReferenceList(const AbiReferenceList & other);
  AbiReferenceList(AbiReferenceList && other) noexcept;
  AbiReferenceList & operator=(const AbiReferenceList & other);
  AbiReferenceList & operator=(AbiReferenceList && other) noexcept;
  ~AbiReferenceList();

  bool resolved() const;
  bool empty() const;
  std::size_t size() const;
  void push_name(const std::string & name);
  void push_resolved(std::size_t id);
  const std::vector<std::string> & names() const;
  const std::vector<std::size_t> & resolved_ids() const;

private:
  enum Mode { NAMES, RESOLVED } mode_;
  union Storage
  {
    Storage() {}
    ~Storage() {}
    std::vector<std::string> names;
    std::vector<std::size_t> resolved;
  } storage_;

  void destroy();
  void copy(const AbiReferenceList & other);
  void move(AbiReferenceList & other) noexcept;
};

}  // namespace abi_mangle
