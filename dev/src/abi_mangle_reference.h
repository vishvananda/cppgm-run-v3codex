#pragma once

// Compact storage for either textual PA14 references or resolved production
// graph handles.

#include <cstddef>
#include <cstdint>
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

// ABI types have two adjacent presentation-name ranges: namespace-lambda
// qualifiers followed by ABI tags.  The standalone adapter owns arbitrary
// spellings, while production carries graph string IDs.  Keep the range split
// in the padding beside one vector union instead of paying for two vectors in
// every type fact.
class AbiTypePresentationNames
{
public:
  AbiTypePresentationNames();
  AbiTypePresentationNames(const AbiTypePresentationNames & other);
  AbiTypePresentationNames(AbiTypePresentationNames && other) noexcept;
  AbiTypePresentationNames & operator=(
    const AbiTypePresentationNames & other);
  AbiTypePresentationNames & operator=(
    AbiTypePresentationNames && other) noexcept;
  ~AbiTypePresentationNames();

  bool resolved() const;
  bool empty() const;
  std::size_t size() const;
  std::size_t namespace_size() const;
  std::size_t tag_size() const;
  void push_namespace_name(const std::string & name);
  void push_tag_name(const std::string & name);
  void push_namespace_resolved(std::size_t id);
  void push_tag_resolved(std::size_t id);
  const std::vector<std::string> & names() const;
  const std::vector<std::size_t> & resolved_ids() const;

private:
  enum Mode : std::uint8_t { NAMES, RESOLVED } mode_;
  std::uint32_t namespace_count_;
  union Storage
  {
    Storage() {}
    ~Storage() {}
    std::vector<std::string> names;
    std::vector<std::size_t> resolved;
  } storage_;

  void prepare_resolved();
  void require_namespace_capacity() const;
  void destroy();
  void copy(const AbiTypePresentationNames & other);
  void move(AbiTypePresentationNames & other) noexcept;
};

}  // namespace abi_mangle
