# Proposed PA29 Tests

This directory holds native-lowering regression candidates whose behavior is
valid but whose machine-IR shape differs from the course reference compiler.
They are not part of the active golden suite.

`discarded-slots-do-not-reserve-frame.t` checks that scalar slots proven to
contain only dead stores do not consume frame space.  The current compiler
removes the three dead stores and emits one retained slot in a 16-byte frame.
The reference compiler retains all four slots and emits an 80-byte frame, so
its generated MIR cannot be used as the oracle for this optimization.

`constant-byte-store-coalescing.t` checks the runtime result of initializing
16 adjacent bytes.  It exercises the native encoder's coalescing of repeated
constant byte stores without changing MIR.  The reference program exits
successfully, but its MIR assigns the preserved base and derived addresses to
different physical registers (and retains unrelated `main` scratch space), so
the reference dump cannot serve as this compiler's structural fixture.

`flag-safe-zero-materialization.t` exercises zero-valued register results and
a compare-to-branch consumer.  Its only new purpose is to inspect that dead
condition flags permit the shorter `xor r32,r32` encoding while the textual
MIR remains a `mov` of zero.  Program behavior alone would duplicate active
coverage, and the course harness does not compare native instruction bytes,
so the representation-specific candidate remains proposed.

`zero-compare-test-encoding.t` covers u32 and i64 comparisons with zero across
unsigned and equality predicates.  It is intended to inspect that the native
bytes use width-correct `test reg,reg` instructions while the baseline MIR
remains `cmp ..., 0`.  Active PA29 tests already cover the behavior and the
harness does not compare native bytes, so this representation-only candidate
remains proposed.

`dead-address-copy-store-folding.t` creates a copied pointer consumed as a
store address and then overwrites the copied register without reading it.  It
is intended to inspect that native encoding stores through the original
pointer and omits the address copy while preserving textual MIR.  Runtime
indirect-store behavior is already actively covered and PA29 has no
native-byte oracle, so it remains proposed.

`dead-address-copy-index-store-folding.t` creates a copied base, constant
index, and store followed by an overwrite of the derived-address register.  It
is intended to inspect that native encoding folds both setup instructions into
the store while retaining the `mov`/`lea`/`store` MIR chain.  Runtime behavior
is already actively covered and PA29 has no native-byte oracle, so it remains
proposed.

`narrow-zero-extension-encoding.t` covers byte and word zero extension.  It is
intended to inspect that `movzx` writes a 32-bit destination, relying on the
x86-64 zeroing rule instead of carrying an unnecessary `REX.W`.  Active PA29
tests already cover extension behavior and the harness does not compare native
bytes, so this representation-only candidate remains proposed.

`dead-address-load-folding.t` creates an adjacent address calculation and load
whose temporary address register is overwritten before it is read again.  It
is intended to inspect that native encoding folds the two displacements into
the load while preserving the textual MIR.  Its program behavior duplicates
active address/load coverage and the harness does not compare native bytes, so
this representation-only candidate remains proposed.

`dead-address-copy-load-folding.t` creates an adjacent register copy and load
whose copied address register is overwritten before it is read again.  It is
intended to inspect that native encoding addresses the load through the
original register and omits the copy while preserving textual MIR.  Program
behavior duplicates active indirect-load coverage and native bytes are not an
active PA29 oracle, so this representation-only candidate remains proposed.

`dead-address-copy-index-load-folding.t` extends the copy/load candidate with
an intervening constant index.  It is intended to inspect that native encoding
folds both the copy and indexed address setup into the load while retaining all
three textual MIR instructions.  Its behavior duplicates active address and
load coverage and native bytes are not an active PA29 oracle, so it remains
proposed.

`dead-address-store-folding.t` creates an indexed address consumed by a store
and then overwrites the address register without reading it.  It is intended
to inspect that native encoding folds the index displacement into the store
while retaining the `lea` and `store` in textual MIR.  Runtime indexed-store
behavior is already actively covered and PA29 has no native-byte oracle, so it
remains proposed.

`dead-copy-store-folding.t` creates a register copy consumed by a store and
then overwrites the copied register without reading it.  It is intended to
inspect that native encoding stores from the original register and omits the
copy while retaining both textual MIR instructions.  Runtime store behavior
is already actively covered and PA29 has no native-byte oracle, so it remains
proposed.

`redundant-u32-normalization-encoding.t` widens `u32` values loaded from global
and frame storage and returned from a call.  The MIR deliberately retains the
explicit `zext i32` required by PA29.  The native encoder may omit it after a
32-bit load or fuse a preceding 64-bit register copy and normalization into
one 32-bit move.  The frame case crosses a call so it cannot use store/load
forwarding; active narrow and same-register frame tests cover forwarding
behavior.  PA29 has no native-byte oracle, so this representation-only witness
remains proposed.

`transient-scratch-address-folding.t` returns both one- and two-eightbyte
objects from frame homes.  The one-eightbyte return deliberately leaves an
adjacent `lea r11, frame` plus one load in MIR; native encoding may address the
load directly because the transient R11 setup has no later reader.  The
two-eightbyte return is the safety boundary: both loads share R11, so its setup
must remain.  Existing active object-return tests cover behavior, while PA29
has no native-byte oracle, so this representation-only witness remains
proposed.

`single-block-call-argument-coalescing.t` materializes a global address and
passes it as the sole integer argument to a call.  A sole-use symbolic address
must be placed directly in the ABI argument register in serialized MIR; it must
not require an intermediate general-purpose register.

`indexed-memory-addressing.t` exercises variable scaled-index loads and stores.
The MIR load and store must each retain the base, index, and scale in one memory
operand, and the native instruction must use the corresponding x86 addressing
mode without separately multiplying or adding the index.

`index-address-placement.t` exercises constant and variable indexed addresses
returned from functions.  MIR must form each address directly in the ABI return
register.  A constant index uses one `lea` from the base register, and a legal
x86 variable scale stays in the `lea` memory operand without separate copy,
multiply, or add instructions.

`direct-return-placement.t` exercises immediately returned integer constants,
integer loads, global addresses, and frame addresses.  Each producer must write
the ABI return register directly, without first assigning a general-purpose
temporary that is used only by the return.
