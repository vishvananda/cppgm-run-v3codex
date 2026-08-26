TARGET = lowir2native
CXX = g++
CPPGM_STDLIB_FLAGS ?=
CPPGM_TEST_APP ?= ../dev/$(TARGET)
REF_TEST_APP ?= $(CPPGM_TEST_APP)-ref
TEST_DEPS ?= dev-shared-target
CC_FLAGS = -std=gnu++11 -Wall -O3 $(CPPGM_STDLIB_FLAGS)
SRC = ../dev/src
GENERATED = ../obj/generated
INC = -I$(SRC) -I$(GENERATED)
OBJ = ../obj
VPATH = $(SRC)
RDEPDIR = $(OBJ)/release/.d
DDEPDIR = $(OBJ)/debug/.d
COURSE_TEST_ROOT = $(wildcard course/pa29)
COURSE_ORACLE_TEST_ROOTS = $(wildcard course/pa29/strict course/pa29/structural course/pa29/behavior)
COURSE_CONTROL_TEST_ROOT ?= course/pa29/controls

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
ifneq ($(CPPGM_BATCH_TESTS),)
	@perl ../scripts/run_lowir_native_tests_worker.pl $(CPPGM_TEST_APP) my tests
else
	@scripts/run_all_tests.pl $(CPPGM_TEST_APP) my tests
endif
	@scripts/compare_results.pl ref my tests

else
test: $(TEST_DEPS)
ifneq ($(CPPGM_BATCH_TESTS),)
	@perl ../scripts/run_lowir_native_tests_worker.pl $(CPPGM_TEST_APP) my tests
else
	@scripts/run_all_tests.pl $(CPPGM_TEST_APP) my tests
endif
	@scripts/compare_results.pl ref my tests
ifneq ($(CPPGM_BATCH_TESTS),)
	@set -e; for root in $(COURSE_ORACLE_TEST_ROOTS); do \
		perl ../scripts/run_lowir_native_tests_worker.pl \
			$(CPPGM_TEST_APP) my "$$root"; \
		case "$$root" in \
			*/behavior) mode=mir_behavior_t ;; \
			*/structural) mode=mir_structural_t ;; \
			*) mode=mir_t ;; \
		esac; \
		perl ../scripts/compare_results_common.pl \
			"$$mode" ref my "$$root"; \
	done
else
	@set -e; for root in $(COURSE_ORACLE_TEST_ROOTS); do \
		scripts/run_all_tests.pl $(CPPGM_TEST_APP) my "$$root"; \
		case "$$root" in \
			*/behavior) mode=mir_behavior_t ;; \
			*/structural) mode=mir_structural_t ;; \
			*) mode=mir_t ;; \
		esac; \
		perl ../scripts/compare_results_common.pl \
			"$$mode" ref my "$$root"; \
	done
endif
	@if [ -d "$(COURSE_CONTROL_TEST_ROOT)" ]; then \
		perl ../scripts/check_pa29_native_contracts.pl \
			$(CPPGM_TEST_APP) "$(COURSE_CONTROL_TEST_ROOT)"; \
		perl ../scripts/check_pa29_native_contracts.pl \
			$(CPPGM_TEST_APP) \
			course/pa29/behavior/scratch-carried-frame-reloads.t; \
		perl ../scripts/check_pa29_native_contracts.pl \
			$(CPPGM_TEST_APP) \
			course/pa29/behavior/deferred-address-parameter-carrier-reuse.t; \
		perl ../scripts/check_pa29_native_contracts.pl \
			$(CPPGM_TEST_APP) \
			course/pa29/behavior/cost-directed-small-zeroinit.t; \
	fi
endif

ref-test:
	@$(REF_TEST_RUN)
	@if [ -d tests/structural ]; then perl ../scripts/canonicalize_lowir_native_refs.pl tests/structural; fi
	@if [ -d course/pa29/structural ]; then perl ../scripts/canonicalize_lowir_native_refs.pl course/pa29/structural; fi

REF_TEST_RUN = case "$(TEST)" in \
	$(COURSE_CONTROL_TEST_ROOT)/*) echo "$(TEST): focused native predicate has no reference sidecar" ;; \
	*) scripts/run_all_tests.pl $(REF_TEST_APP) ref "$(if $(strip $(TEST)),$(TEST),tests)" ;; \
esac

CHECK_RUN = case "$(TEST)" in \
	$(COURSE_CONTROL_TEST_ROOT)/*) perl ../scripts/check_pa29_native_contracts.pl $(CPPGM_TEST_APP) "$(TEST)" ;; \
	*) scripts/run_all_tests.pl $(CPPGM_TEST_APP) check "$(TEST)" ;; \
esac
CHECK_COMPARE = case "$(TEST)" in \
	$(COURSE_CONTROL_TEST_ROOT)/*) true ;; \
	*) scripts/compare_results.pl ref check "$(TEST)" ;; \
esac

include ../scripts/pa_run_check_targets.mk

.PHONY: dev-shared-target ref-test
$(CPPGM_TEST_APP):
	$(MAKE) -C ../dev $(TARGET) CXX=$(CXX) OBJ=$(OBJ)

dev-shared-target:
	@if [ "$(CPPGM_SKIP_DEV_REBUILD)" = "1" ]; then true; else $(MAKE) -C ../dev $(TARGET) CXX=$(CXX) OBJ=$(OBJ); fi

clean:
	-rm -f $(TARGET) $(TARGET)-debug $(OBJ)/release/*.o $(OBJ)/debug/*.o

$(RDEPDIR)/%.d: ;
.PRECIOUS: $(RDEPDIR)/%.d

$(DDEPDIR)/%.d: ;
.PRECIOUS: $(DDEPDIR)/%.d

-include $(addprefix $(RDEPDIR)/,$(addsuffix .d,$(common_obj_basenames)))
-include $(addprefix $(DDEPDIR)/,$(addsuffix .d,$(common_obj_basenames)))
