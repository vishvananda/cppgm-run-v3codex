# Proposed PA16 tests

`local-class-default-member-enclosing-constant.t` checks that a local class's
default member initializer can use the value of an enclosing automatic const
integral when the lvalue-to-rvalue conversion is a constant expression. GCC
and Clang accept the source and its program returns zero. The course reference
currently fails while lowering the initializer, so this case is retained here
rather than used as an active course oracle.
