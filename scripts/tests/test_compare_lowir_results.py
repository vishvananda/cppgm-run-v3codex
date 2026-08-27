#!/usr/bin/env python3

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
COMPARE = REPO_ROOT / "scripts" / "compare_results_common.pl"


REFERENCE = """function @main(%left : i64, %right : i64) -> i64 {
  slot $saved : i64

  block ^entry:
    %sum = binary add i64 %left, %right
    store i64 %sum, $saved
    jump ^done

  block ^done:
    %result = load i64 $saved
    return i64 %result
}
"""


RENAMED = """function @main(%a : i64, %b : i64) -> i64 {
  slot $__o1inl0__saved : i64

  block ^__o1inl0__entry:
    %__o1inl0__sum = binary add i64 %a, %b
    store i64 %__o1inl0__sum, $__o1inl0__saved
    jump ^__o1inl0__done

  block ^__o1inl0__done:
    %__o1inl0__result = load i64 $__o1inl0__saved
    return i64 %__o1inl0__result
}
"""


class LowIrComparisonTest(unittest.TestCase):
    def compare(self, reference, generated, *, direct=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            test = root / "case.t"
            test.write_text(reference, encoding="utf-8")
            (root / "case.ref").write_text(reference, encoding="utf-8")
            (root / "case.my").write_text(generated, encoding="utf-8")
            for suffix in ("ref", "my"):
                (root / f"case.{suffix}.exit_status").write_text(
                    "EXIT_SUCCESS\n", encoding="utf-8"
                )
            environment = os.environ.copy()
            if direct:
                environment["CPPGM_LOWIR_DIRECT_TEXT_COMPARE"] = "1"
            return subprocess.run(
                [
                    "perl",
                    str(COMPARE),
                    "lowir_t",
                    "ref",
                    "my",
                    str(test),
                ],
                cwd=REPO_ROOT,
                env=environment,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

    def test_relaxed_compare_ignores_function_local_names(self):
        result = self.compare(REFERENCE, RENAMED)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_lowir_sanity_accepts_phi_loop_backedge_value(self):
        program = """function @main() -> i64 {
  block ^entry:
    jump ^loop

  block ^loop:
    %current = phi i64 [^entry: 0, ^latch: %next]
    %done = cmp ge i64 %current, 3
    branch %done, ^exit, ^latch

  block ^latch:
    %next = binary add i64 %current, 1
    jump ^loop

  block ^exit:
    return i64 %current
}
"""
        result = self.compare(program, program)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_retains_operand_identity(self):
        changed = RENAMED.replace(
            "binary add i64 %a, %b", "binary add i64 %a, %a"
        )
        result = self.compare(REFERENCE, changed)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match reference", result.stdout)

    def test_relaxed_compare_retains_control_flow_identity(self):
        changed = RENAMED.replace(
            "jump ^__o1inl0__done", "jump ^__o1inl0__entry"
        )
        result = self.compare(REFERENCE, changed)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match reference", result.stdout)

    def test_direct_compare_still_requires_exact_names(self):
        result = self.compare(REFERENCE, RENAMED, direct=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("direct text compare", result.stdout)

    def test_relaxed_compare_pairs_global_presentation_by_object_identity(self):
        reference = """declare global @rtti : ptr [object=_ZTI1X]

global @vtable [object=_ZTV1X] = {
  ptr addr @rtti
}

function @main() -> i64 {
  block ^entry:
    %address = addr @vtable
    return i64 0
}
"""
        generated = """declare global @__typed_rtti : ptr [role=rtti_data, object=_ZTI1X]

global @__typed_vtable [object=_ZTV1X] = {
  ptr addr @__typed_rtti
}

function @main() -> i64 {
  block ^entry:
    %address = addr @__typed_vtable
    return i64 0
}
"""
        result = self.compare(reference, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_retains_global_initializer_shape(self):
        reference = """global @left : i64 = 1

function @main() -> i64 {
  block ^entry:
    return i64 0
}
"""
        generated = reference.replace("= 1", "= 2")
        result = self.compare(reference, generated)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match reference", result.stdout)

    def test_relaxed_compare_normalizes_trivial_lifecycle_calls(self):
        reference = """function @main() -> i64 {
  slot $value : obj<1x1>
  block ^entry:
    %address = addr $value
    call void @empty_ctor(%address)
    return i64 0
}

function @empty_ctor(%this : ptr) -> void [binding=weak, trivial_lifecycle=yes] {
  slot $saved : ptr
  block ^entry:
    store ptr %this, $saved
    return void
}
"""
        generated = """function @main() -> i64 {
  slot $value : obj<1x1>
  block ^entry:
    %address = addr $value
    return i64 0
}
"""
        result = self.compare(reference, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_rejects_generated_trivial_lifecycle_metadata(self):
        generated = REFERENCE.replace(
            ") -> i64 {", ") -> i64 [trivial_lifecycle=yes] {", 1
        )
        result = self.compare(REFERENCE, generated)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("removed trivial_lifecycle metadata", result.stdout)

    def test_relaxed_compare_normalizes_legacy_unreachable_call(self):
        reference = """declare function @impossible() -> void [role=unreachable, effects=readnone, unwind=no, return=noreturn]

function @trap() -> void {
  block ^entry:
    call void @impossible()
    return void
}
"""
        generated = """function @trap() -> void {
  block ^entry:
    unreachable
}
"""
        result = self.compare(reference, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_rejects_generated_unreachable_role(self):
        generated = REFERENCE.replace(
            ") -> i64 {", ") -> i64 [role=unreachable] {", 1
        )
        result = self.compare(REFERENCE, generated)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("removed unreachable role", result.stdout)

    def test_lowir_sanity_rejects_removed_access_none(self):
        generated = REFERENCE.replace(
            "%left : i64", "%left : ptr [access=none]", 1
        )
        result = self.compare(REFERENCE, generated)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown parameter access mode 'none'", result.stdout)

    def test_lowir_sanity_rejects_removed_pass_decay(self):
        generated = REFERENCE.replace(
            "%left : i64", "%left : ptr [pass=decay]", 1
        )
        result = self.compare(REFERENCE, generated)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("removed decay surface", result.stdout)

    def test_relaxed_compare_rejects_generated_capture_metadata(self):
        generated = REFERENCE.replace(
            "%left : i64", "%left : ptr [capture=nocapture]", 1
        )
        result = self.compare(REFERENCE, generated)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("removed capture/access parameter metadata", result.stdout)

    def test_relaxed_compare_rejects_generated_access_metadata(self):
        generated = REFERENCE.replace(
            "%left : i64", "%left : ptr [access=read]", 1
        )
        result = self.compare(REFERENCE, generated)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("removed capture/access parameter metadata", result.stdout)

    def test_relaxed_compare_rejects_generated_source_origin_projection(self):
        generated = REFERENCE.replace(
            "%sum = binary add i64 %left, %right",
            "%projected = index i8 [projection=base_subobject] nullptr, 0\n"
            "    %sum = binary add i64 %left, %right",
        )
        result = self.compare(REFERENCE, generated)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("removed source-origin index projection", result.stdout)

    def test_relaxed_compare_accepts_legacy_source_origin_projection_reference(self):
        reference = REFERENCE.replace(
            "%sum = binary add i64 %left, %right",
            "%projected = index i8 [projection=reference_field] nullptr, 0\n"
            "    %sum = binary add i64 %left, %right",
        )
        generated = reference.replace(" [projection=reference_field]", "")
        result = self.compare(reference, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_pairs_legacy_decay_by_ignored_shape(self):
        reference = """function @read_old(%value : ptr [pass=decay]) -> i32 {
  block ^entry:
    %loaded = load i32 %value
    return i32 %loaded
}

function @observe_old(%value : ptr [pass=decay]) -> void {
  block ^entry:
    return void
}

function @main() -> i32 [role=entry] {
  slot $value : i32

  block ^entry:
    %address = addr $value
    call void @observe_old(%address)
    %result = call i32 @read_old(%address)
    return i32 %result
}
"""
        generated = """function @observe_new(%value : ptr) -> void {
  block ^entry:
    return void
}

function @read_new(%value : ptr) -> i32 {
  block ^entry:
    %loaded = load i32 %value
    return i32 %loaded
}

function @main() -> i32 [role=entry] {
  slot $value : i32

  block ^entry:
    %address = addr $value
    call void @observe_new(%address)
    %result = call i32 @read_new(%address)
    return i32 %result
}
"""
        result = self.compare(reference, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_lowir_sanity_rejects_removed_unary_decay(self):
        generated = REFERENCE.replace(
            "%sum = binary add i64 %left, %right",
            "%decayed = unary decay ptr nullptr\n"
            "    %sum = binary add i64 %left, %right",
        )
        result = self.compare(REFERENCE, generated)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("removed decay surface", result.stdout)

    def test_relaxed_compare_erases_legacy_unary_decay(self):
        reference = REFERENCE.replace(
            "%sum = binary add i64 %left, %right",
            "%pointer = copy ptr nullptr\n"
            "    %decayed = unary decay ptr %pointer\n"
            "    %sum = binary add i64 %left, %right",
        )
        generated = REFERENCE.replace(
            "%sum = binary add i64 %left, %right",
            "%pointer = copy ptr nullptr\n"
            "    %sum = binary add i64 %left, %right",
        )
        result = self.compare(reference, generated)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_relaxed_compare_retains_nontrivial_calls(self):
        reference = REFERENCE.replace(
            "%sum = binary add i64 %left, %right",
            "call void @effect()\n    %sum = binary add i64 %left, %right",
        ).replace(
            "function @main",
            "declare function @effect() -> void\n\nfunction @main",
        )
        result = self.compare(reference, REFERENCE)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match reference", result.stdout)

    def test_relaxed_compare_allows_additional_generated_object_root(self):
        generated = REFERENCE.replace(
            ") -> i64 {", ") -> i64 [object_root=yes] {", 1
        )
        result = self.compare(REFERENCE, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_requires_reference_object_root(self):
        reference = REFERENCE.replace(
            ") -> i64 {", ") -> i64 [object_root=yes] {", 1
        )
        result = self.compare(reference, REFERENCE)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match reference", result.stdout)

    def test_relaxed_compare_allows_later_generated_function_refinement(self):
        generated = REFERENCE.replace(
            "%sum = binary add i64 %left, %right",
            "call void @later_helper()\n    %sum = binary add i64 %left, %right",
        ) + """\nfunction @later_helper() -> void {
  block ^entry:
    return void
}
"""
        result = self.compare(REFERENCE, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_allows_declaration_to_definition_refinement(self):
        reference = "declare function @helper() -> i64\n"
        generated = """function @helper() -> i64 {
  block ^entry:
    return i64 0
}
"""
        result = self.compare(reference, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_normalizes_tls_wrapper_access(self):
        reference = """declare global @value : i32 [storage=thread_local]
declare function @wrapper() -> ptr [tls_for=@value]

function @main() -> i32 {
  block ^entry:
    %address = addr @value
    %result = load i32 %address
    return i32 %result
}
"""
        generated = reference.replace(
            "%address = addr @value", "%address = call ptr @wrapper()"
        )
        result = self.compare(reference, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_normalizes_direct_tls_load(self):
        reference = """declare function @wrapper() -> ptr [tls_for=@value]
global @value : i32 [storage=thread_local] = 1

function @main() -> i32 {
  block ^entry:
    %result = load i32 @value
    return i32 %result
}
"""
        generated = reference.replace(
            "%result = load i32 @value",
            "%address = call ptr @wrapper()\n    %result = load i32 %address",
        )
        result = self.compare(reference, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_allows_unretained_unused_declaration(self):
        reference = "declare function @unused() -> void\n\n" + REFERENCE
        result = self.compare(reference, REFERENCE)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_allows_unretained_unused_weak_body(self):
        reference = REFERENCE + """
function @unused() -> void [binding=weak] {
  block ^entry:
    return void
}
"""
        result = self.compare(reference, REFERENCE)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_requires_referenced_weak_body(self):
        reference = REFERENCE.replace(
            "%sum = binary add i64 %left, %right",
            "call void @needed()\n    %sum = binary add i64 %left, %right",
        ) + """
function @needed() -> void [binding=weak] {
  block ^entry:
    return void
}
"""
        result = self.compare(reference, REFERENCE)
        self.assertNotEqual(result.returncode, 0)

    def test_relaxed_projection_alpha_renames_inserted_locals(self):
        generated = RENAMED.replace(
            "slot $__o1inl0__saved : i64",
            "slot $inserted : ptr\n  slot $__o1inl0__saved : i64",
        ).replace(
            "block ^__o1inl0__entry:",
            "block ^inserted:\n    jump ^__o1inl0__entry\n\n"
            "  block ^__o1inl0__entry:",
        )
        result = self.compare(REFERENCE, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_allows_registered_static_finalizer(self):
        reference = """declare function @destroy(%value : ptr) -> void

global @value : i32 = 0
global @guard : i64 = 0

function @get() -> ptr {
  block ^entry:
    %address = addr @value
    return ptr %address
}

function @fini() -> void [role=fini] {
  block ^entry:
    %ready = load i64 @guard
    %needed = cmp ne i64 %ready, 0
    branch %needed, ^destroy, ^done
  block ^destroy:
    %address = addr @value
    call void @destroy(%address)
    jump ^done
  block ^done:
    return void
}
"""
        generated = """declare function @destroy(%value : ptr) -> void
declare function @atexit(%callback : ptr) -> i32

global @value : i32 = 0
global @guard : i64 = 0

function @get() -> ptr {
  block ^entry:
    %registered = call i32 @atexit(@helper)
    %address = addr @value
    return ptr %address
}

function @helper() -> void {
  block ^entry:
    %address = addr @value
    call void @destroy(%address)
    return void
}
"""
        result = self.compare(reference, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_relaxed_compare_allows_complete_nothrow_termination_path(self):
        reference = """function @work() -> void [unwind=no] {
  block ^entry:
    eh_try ^cleanup
    eh_end
    return void
  block ^cleanup:
    resume
}
"""
        generated = """function @work() -> void [unwind=no] {
  slot $exception : ptr
  block ^entry:
    eh_try ^cleanup
    eh_end
    return void
  block ^cleanup:
    eh_catch_all, 1
    %exception = exception ptr
    store ptr %exception, $exception
    call void @terminate()
    return void
}

declare function @terminate() -> void
"""
        result = self.compare(reference, generated)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
