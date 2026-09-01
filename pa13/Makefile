TARGET = lowir2cy86
CXX ?= g++
CPPGM_HOST_CXX ?= $(CXX)
CPPGM_STDLIB_FLAGS ?=
CPPGM_TEST_APP ?= ../dev/$(TARGET)
LOWIROPT_APP ?= ../dev/lowiropt
REF_TEST_APP ?= $(CPPGM_TEST_APP)-ref
DEBUGINFO_LOWIROPT_REF_APP ?= ../dev/lowiropt-ref
DEBUGINFO_CPPGM_REF_APP ?= ../dev/cppgm++-ref
DEBUGINFO_LOWIR2NATIVE_REF_APP ?= ../dev/lowir2native-ref
TEST_DEPS ?= dev-shared-target cy86-target lowiropt-target
CY86_APP ?= ../dev/cy86
COURSE_CONTROL_TEST_ROOT ?= course/pa13/controls
CC_FLAGS = -std=gnu++11 -Wall -O3 $(CPPGM_STDLIB_FLAGS)
SRC = ../dev/src
GENERATED = ../obj/generated
INC = -I$(SRC) -I$(GENERATED)
OBJ = ../obj
VPATH = $(SRC)
RDEPDIR = $(OBJ)/release/.d
DDEPDIR = $(OBJ)/debug/.d
COURSE_TEST_ROOT = $(wildcard course/pa13)
LOCAL_TEST_ROOT = tests/spec
REF_TEST_ROOT = $(LOCAL_TEST_ROOT)
DEBUGINFO_TEST_ROOT = tests/debuginfo
DEBUGINFO_SOURCE_G0_TEST_ROOT = $(DEBUGINFO_TEST_ROOT)/source-g0
DEBUGINFO_MACHINE_TEST_ROOT = $(DEBUGINFO_TEST_ROOT)/machine
DEBUGINFO_OBJECT_G0_TEST_ROOT = $(DEBUGINFO_TEST_ROOT)/object-g0
DEBUGINFO_OBJECT_TEST_ROOT = $(DEBUGINFO_TEST_ROOT)/object
DEBUGINFO_OBJECT_O1_TEST_ROOT = $(DEBUGINFO_TEST_ROOT)/object-o1
DEBUGINFO_OBJECT_O2_TEST_ROOT = $(DEBUGINFO_TEST_ROOT)/object-o2
DEBUGINFO_EXECUTABLE_G0_TEST_ROOT = $(DEBUGINFO_TEST_ROOT)/executable-g0
DEBUGINFO_EXECUTABLE_MULTI_TEST_ROOT = $(DEBUGINFO_TEST_ROOT)/executable-multi
DEBUGINFO_EXECUTABLE_TEST_ROOT = $(DEBUGINFO_TEST_ROOT)/executable
DEBUGINFO_EXECUTABLE_O1_TEST_ROOT = $(DEBUGINFO_TEST_ROOT)/executable-o1
DEBUGINFO_EXECUTABLE_O2_TEST_ROOT = $(DEBUGINFO_TEST_ROOT)/executable-o2

$(shell mkdir -p $(RDEPDIR) $(DDEPDIR) >/dev/null)

include ../dev/frontend_source_sets.mk

common_obj_basenames = $(FRONTEND_OBJ_BASENAMES_$(TARGET))
common_objs = $(addprefix $(OBJ)/$(1)/,$(addsuffix .o,$(common_obj_basenames)))

all: dev-shared-target

$(OBJ)/release $(OBJ)/debug: %:
	@- mkdir -p $@

$(OBJ)/release/%.o: %.cpp
	@mkdir -p $(@D) $(dir $(RDEPDIR)/$*)
	$(CXX) $(CC_FLAGS) $(INC) -c -MT $@ -MMD -MP -MF $(RDEPDIR)/$*.Td -o $@ $<
	mv -f $(RDEPDIR)/$*.Td $(RDEPDIR)/$*.d

$(OBJ)/debug/%.o: %.cpp
	@mkdir -p $(@D) $(dir $(DDEPDIR)/$*)
	$(CXX) $(CC_FLAGS) $(INC) -c -MT $@ -MMD -MP -MF $(DDEPDIR)/$*.Td -o $@ $<
	mv -f $(DDEPDIR)/$*.Td $(DDEPDIR)/$*.d

ifeq ($(COURSE_TEST_ROOT),)
test: $(TEST_DEPS)
	@scripts/run_all_tests.pl $(CPPGM_TEST_APP) my $(LOCAL_TEST_ROOT)
	@scripts/compare_results.pl ref my $(LOCAL_TEST_ROOT)
else
test: $(TEST_DEPS)
	@scripts/run_all_tests.pl $(CPPGM_TEST_APP) my $(LOCAL_TEST_ROOT)
	@scripts/compare_results.pl ref my $(LOCAL_TEST_ROOT)
	@scripts/run_all_tests.pl $(CPPGM_TEST_APP) my $(COURSE_TEST_ROOT)
	@scripts/compare_results.pl ref my $(COURSE_TEST_ROOT)
	@if [ -d "$(COURSE_CONTROL_TEST_ROOT)" ]; then \
		perl ../scripts/check_pa13_eh_defaults.pl $(CPPGM_TEST_APP) \
			$(CY86_APP) "$(COURSE_CONTROL_TEST_ROOT)"; \
		perl ../scripts/check_pa13_unreachable_terminator.pl \
			$(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) \
			"$(COURSE_CONTROL_TEST_ROOT)"; \
		perl ../scripts/check_pa13_removed_parameter_metadata.pl \
			$(LOWIROPT_APP) "$(COURSE_CONTROL_TEST_ROOT)"; \
		perl ../scripts/check_pa13_removed_instructions.pl \
			$(LOWIROPT_APP) "$(COURSE_CONTROL_TEST_ROOT)"; \
		perl ../scripts/check_pa13_index_projection_contract.pl \
			$(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) \
			"$(COURSE_CONTROL_TEST_ROOT)"; \
		perl ../scripts/check_pa13_call_boundary_metadata.pl \
			$(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) \
			"$(COURSE_CONTROL_TEST_ROOT)"; \
		perl ../scripts/check_pa13_object_extent.pl \
			$(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) \
			"$(COURSE_CONTROL_TEST_ROOT)"; \
		perl ../scripts/check_pa13_stable_prefix_query.pl \
			$(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) \
			"$(COURSE_CONTROL_TEST_ROOT)"; \
		perl ../scripts/check_pa13_copy_elision_metadata.pl \
			$(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) \
			"$(COURSE_CONTROL_TEST_ROOT)"; \
		perl ../scripts/check_pa13_global_section.pl \
			$(LOWIROPT_APP) "$(COURSE_CONTROL_TEST_ROOT)"; \
		perl ../scripts/audit_lowir_contract.pl; \
	fi
endif

ref-test:
	@$(REF_TEST_RUN)

ref-test-debuginfo:
	@CPPGM_LOWIROPT_APP='$(DEBUGINFO_LOWIROPT_REF_APP)' scripts/run_all_tests.pl scripts/lowiropt-o0.sh ref $(DEBUGINFO_TEST_ROOT)/o0
	@CPPGM_CPPGM_APP='$(DEBUGINFO_CPPGM_REF_APP)' scripts/run_all_tests.pl scripts/cppgm-emit-lowir-g0.sh ref $(DEBUGINFO_SOURCE_G0_TEST_ROOT)
	@CPPGM_CPPGM_APP='$(DEBUGINFO_CPPGM_REF_APP)' scripts/run_all_tests.pl scripts/cppgm-emit-lowir-o0.sh ref $(DEBUGINFO_TEST_ROOT)/source
	@CPPGM_LOWIR2NATIVE_APP='$(DEBUGINFO_LOWIR2NATIVE_REF_APP)' scripts/run_all_tests.pl scripts/lowir2native-dump-machine-ir.sh ref $(DEBUGINFO_MACHINE_TEST_ROOT)
	@CPPGM_CPPGM_APP='$(DEBUGINFO_CPPGM_REF_APP)' scripts/run_all_tests.pl scripts/cppgm-dwarfdump-line-g0.sh ref $(DEBUGINFO_OBJECT_G0_TEST_ROOT)
	@CPPGM_CPPGM_APP='$(DEBUGINFO_CPPGM_REF_APP)' scripts/run_all_tests.pl scripts/cppgm-dwarfdump-line-o0.sh ref $(DEBUGINFO_OBJECT_TEST_ROOT)
	@CPPGM_CPPGM_APP='$(DEBUGINFO_CPPGM_REF_APP)' scripts/run_all_tests.pl scripts/cppgm-dwarfdump-line-o1.sh ref $(DEBUGINFO_OBJECT_O1_TEST_ROOT)
	@CPPGM_CPPGM_APP='$(DEBUGINFO_CPPGM_REF_APP)' scripts/run_all_tests.pl scripts/cppgm-dwarfdump-line-o2.sh ref $(DEBUGINFO_OBJECT_O2_TEST_ROOT)
	@CPPGM_CPPGM_APP='$(DEBUGINFO_CPPGM_REF_APP)' scripts/run_all_tests.pl scripts/cppgm-lldb-line-g0.sh ref $(DEBUGINFO_EXECUTABLE_G0_TEST_ROOT)
	@CPPGM_CPPGM_APP='$(DEBUGINFO_CPPGM_REF_APP)' scripts/run_all_tests.pl scripts/cppgm-lldb-multi-o0.sh ref $(DEBUGINFO_EXECUTABLE_MULTI_TEST_ROOT)
	@CPPGM_CPPGM_APP='$(DEBUGINFO_CPPGM_REF_APP)' scripts/run_all_tests.pl scripts/cppgm-lldb-line-o0.sh ref $(DEBUGINFO_EXECUTABLE_TEST_ROOT)
	@CPPGM_CPPGM_APP='$(DEBUGINFO_CPPGM_REF_APP)' scripts/run_all_tests.pl scripts/cppgm-lldb-line-o1.sh ref $(DEBUGINFO_EXECUTABLE_O1_TEST_ROOT)
	@CPPGM_CPPGM_APP='$(DEBUGINFO_CPPGM_REF_APP)' scripts/run_all_tests.pl scripts/cppgm-lldb-line-o2.sh ref $(DEBUGINFO_EXECUTABLE_O2_TEST_ROOT)

CHECK_RUN = case "$(TEST)" in \
	$(COURSE_CONTROL_TEST_ROOT)/*default-eh-*) perl ../scripts/check_pa13_eh_defaults.pl $(CPPGM_TEST_APP) $(CY86_APP) "$(TEST)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*unreachable*) perl ../scripts/check_pa13_unreachable_terminator.pl $(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) "$(COURSE_CONTROL_TEST_ROOT)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*removed-unreachable-role*) perl ../scripts/check_pa13_unreachable_terminator.pl $(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) "$(COURSE_CONTROL_TEST_ROOT)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*removed-access-*) perl ../scripts/check_pa13_removed_parameter_metadata.pl $(LOWIROPT_APP) "$(TEST)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*removed-capture-*) perl ../scripts/check_pa13_removed_parameter_metadata.pl $(LOWIROPT_APP) "$(TEST)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*removed-pass-decay*) perl ../scripts/check_pa13_removed_parameter_metadata.pl $(LOWIROPT_APP) "$(TEST)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*removed-pass-reference*) perl ../scripts/check_pa13_removed_parameter_metadata.pl $(LOWIROPT_APP) "$(TEST)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*removed-unary-decay*) perl ../scripts/check_pa13_removed_instructions.pl $(LOWIROPT_APP) "$(TEST)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*index-projections*) perl ../scripts/check_pa13_index_projection_contract.pl $(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) "$(TEST)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*-projection*) perl ../scripts/check_pa13_index_projection_contract.pl $(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) "$(TEST)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*retained-call-boundary-metadata*) perl ../scripts/check_pa13_call_boundary_metadata.pl $(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) "$(TEST)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*parameter-object-extent*|$(COURSE_CONTROL_TEST_ROOT)/*object-extent-*) perl ../scripts/check_pa13_object_extent.pl $(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) "$(COURSE_CONTROL_TEST_ROOT)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*stable-prefix-query*) perl ../scripts/check_pa13_stable_prefix_query.pl $(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) "$(COURSE_CONTROL_TEST_ROOT)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*copy-elision-*) perl ../scripts/check_pa13_copy_elision_metadata.pl $(LOWIROPT_APP) $(CPPGM_TEST_APP) $(CY86_APP) "$(COURSE_CONTROL_TEST_ROOT)" ;; \
	$(COURSE_CONTROL_TEST_ROOT)/*global-section*|$(COURSE_CONTROL_TEST_ROOT)/*retained-global-section*) perl ../scripts/check_pa13_global_section.pl $(LOWIROPT_APP) "$(TEST)" ;; \
	*) scripts/run_all_tests.pl $(CPPGM_TEST_APP) check "$(TEST)" ;; \
esac
CHECK_COMPARE = case "$(TEST)" in \
	$(COURSE_CONTROL_TEST_ROOT)/*) true ;; \
	*) scripts/compare_results.pl ref check "$(TEST)" ;; \
esac

include ../scripts/pa_run_check_targets.mk

.PHONY: dev-shared-target cy86-target debuginfo-lowiropt-target debuginfo-cppgm-target debuginfo-lowir2native-target ref-test ref-test-debuginfo
../dev/$(TARGET):
	$(MAKE) -C ../dev $(TARGET) CXX=$(CXX) CPPGM_HOST_CXX=$(CPPGM_HOST_CXX)

dev-shared-target:
	@if [ "$(CPPGM_SKIP_DEV_REBUILD)" = "1" ]; then true; else $(MAKE) -C ../dev $(TARGET) CXX=$(CXX) CPPGM_HOST_CXX=$(CPPGM_HOST_CXX); fi

cy86-target:
	@if [ "$(CPPGM_SKIP_DEV_REBUILD)" = "1" ] && [ -x $(CY86_APP) ]; then true; else $(MAKE) -C ../dev cy86 CXX=$(CXX) CPPGM_HOST_CXX=$(CPPGM_HOST_CXX); fi

lowiropt-target:
	@if [ "$(CPPGM_SKIP_DEV_REBUILD)" = "1" ] && [ -x $(LOWIROPT_APP) ]; then true; else $(MAKE) -C ../dev lowiropt CXX=$(CXX) CPPGM_HOST_CXX=$(CPPGM_HOST_CXX); fi

debuginfo-lowiropt-target:
	@if [ "$(CPPGM_SKIP_DEV_REBUILD)" = "1" ] && [ -x ../dev/lowiropt ]; then true; else $(MAKE) -C ../dev lowiropt CXX=$(CXX) CPPGM_HOST_CXX=$(CPPGM_HOST_CXX); fi

debuginfo-cppgm-target:
	@if [ "$(CPPGM_SKIP_DEV_REBUILD)" = "1" ] && [ -x ../dev/cppgm++ ]; then true; else $(MAKE) -C ../dev cppgm++ CXX=$(CXX) CPPGM_HOST_CXX=$(CPPGM_HOST_CXX); fi

debuginfo-lowir2native-target:
	@if [ "$(CPPGM_SKIP_DEV_REBUILD)" = "1" ] && [ -x ../dev/lowir2native ]; then true; else $(MAKE) -C ../dev lowir2native CXX=$(CXX) CPPGM_HOST_CXX=$(CPPGM_HOST_CXX); fi

test-debuginfo: debuginfo-lowiropt-target debuginfo-cppgm-target debuginfo-lowir2native-target
	@rm -f .test_failed
	@scripts/run_all_tests.pl scripts/lowiropt-o0.sh my $(DEBUGINFO_TEST_ROOT)/o0
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_TEST_ROOT)/o0
	@scripts/run_all_tests.pl scripts/cppgm-emit-lowir-g0.sh my $(DEBUGINFO_SOURCE_G0_TEST_ROOT)
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_SOURCE_G0_TEST_ROOT)
	@scripts/run_all_tests.pl scripts/cppgm-emit-lowir-o0.sh my $(DEBUGINFO_TEST_ROOT)/source
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_TEST_ROOT)/source
	@scripts/run_all_tests.pl scripts/lowir2native-dump-machine-ir.sh my $(DEBUGINFO_MACHINE_TEST_ROOT)
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_MACHINE_TEST_ROOT)
	@scripts/run_all_tests.pl scripts/cppgm-dwarfdump-line-g0.sh my $(DEBUGINFO_OBJECT_G0_TEST_ROOT)
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_OBJECT_G0_TEST_ROOT)
	@scripts/run_all_tests.pl scripts/cppgm-dwarfdump-line-o0.sh my $(DEBUGINFO_OBJECT_TEST_ROOT)
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_OBJECT_TEST_ROOT)
	@scripts/run_all_tests.pl scripts/cppgm-dwarfdump-line-o1.sh my $(DEBUGINFO_OBJECT_O1_TEST_ROOT)
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_OBJECT_O1_TEST_ROOT)
	@scripts/run_all_tests.pl scripts/cppgm-dwarfdump-line-o2.sh my $(DEBUGINFO_OBJECT_O2_TEST_ROOT)
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_OBJECT_O2_TEST_ROOT)
	@scripts/run_all_tests.pl scripts/cppgm-lldb-line-g0.sh my $(DEBUGINFO_EXECUTABLE_G0_TEST_ROOT)
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_EXECUTABLE_G0_TEST_ROOT)
	@scripts/run_all_tests.pl scripts/cppgm-lldb-multi-o0.sh my $(DEBUGINFO_EXECUTABLE_MULTI_TEST_ROOT)
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_EXECUTABLE_MULTI_TEST_ROOT)
	@scripts/run_all_tests.pl scripts/cppgm-lldb-line-o0.sh my $(DEBUGINFO_EXECUTABLE_TEST_ROOT)
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_EXECUTABLE_TEST_ROOT)
	@scripts/run_all_tests.pl scripts/cppgm-lldb-line-o1.sh my $(DEBUGINFO_EXECUTABLE_O1_TEST_ROOT)
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_EXECUTABLE_O1_TEST_ROOT)
	@scripts/run_all_tests.pl scripts/cppgm-lldb-line-o2.sh my $(DEBUGINFO_EXECUTABLE_O2_TEST_ROOT)
	@KEEP_GOING=1 scripts/compare_results.pl ref my $(DEBUGINFO_EXECUTABLE_O2_TEST_ROOT)
	@test ! -f .test_failed

clean:
	-rm -f $(TARGET) $(TARGET)-debug $(OBJ)/release/*.o $(OBJ)/debug/*.o

$(RDEPDIR)/%.d: ;
.PRECIOUS: $(RDEPDIR)/%.d

$(DDEPDIR)/%.d: ;
.PRECIOUS: $(DDEPDIR)/%.d

-include $(addprefix $(RDEPDIR)/,$(addsuffix .d,$(common_obj_basenames)))
-include $(addprefix $(DDEPDIR)/,$(addsuffix .d,$(common_obj_basenames)))
