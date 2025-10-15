
CONFIG_FILE ?= Makefile.config

ifneq (,$(wildcard $(CONFIG_FILE)))
include $(CONFIG_FILE)
endif

SHELL := /bin/bash

PYTHON ?= python3
HDL_CONFIG ?= tools/conf.json
HDL_PARAM_SCRIPT ?= tools/gen_impl_params.py
HDL_PARAM_DIR ?= hdl/tb
HDL_PARAM_FILES := $(HDL_PARAM_DIR)/impl_params.v $(HDL_PARAM_DIR)/synth_params.vhd
COCOTB_LOCAL_DIFF_DIR ?= hdl/cocotb/local_diff
COCOTB_LOCAL_DIFF_VECTORS ?= tests/vectors/local_diff_vectors.csv

VIVADO_ENV := source "$(SETTINGS_SCRIPT)" &&

.PHONY: help cpp cpp-configure cpp-build cpp-test cpp-clean hdl-params hdl-project hdl-sim hdl-clean hdl-cocotb-local-diff clean

help:
	@echo "Available targets:"
	@echo "  make cpp            - Configure, build, and test the C++ sources."
	@echo "  make cpp-build      - Build the C++ executables without running tests."
	@echo "  make cpp-test       - Run the C++ unit tests (builds first)."
	@echo "  make hdl-project    - Generate the Vivado project structure."
	@echo "  make hdl-sim        - Create the Vivado project and run behavioral simulation."
	@echo "  make clean          - Remove generated build artifacts."

$(CPP_BUILD_DIR):
	@mkdir -p $@

cpp-configure: $(CPP_BUILD_DIR)
	cmake -S . -B $(CPP_BUILD_DIR)

cpp-build: cpp-configure
	cmake --build $(CPP_BUILD_DIR)

cpp-test: cpp-build
	cd $(CPP_BUILD_DIR) && ctest --output-on-failure

cpp: cpp-test

cpp-clean:
	rm -rf $(CPP_BUILD_DIR)


$(HDL_BUILD_DIR):
	@mkdir -p $@

hdl-params:
	@mkdir -p $(HDL_PARAM_DIR)
	$(PYTHON) $(HDL_PARAM_SCRIPT) $(HDL_CONFIG)

$(COCOTB_LOCAL_DIFF_VECTORS): tools/gen_local_diff_vectors.py python_reference/local_diff.py $(HDL_CONFIG)
	$(PYTHON) tools/gen_local_diff_vectors.py $(HDL_CONFIG) $(COCOTB_LOCAL_DIFF_VECTORS)

hdl-cocotb-local-diff: $(COCOTB_LOCAL_DIFF_VECTORS)
	$(MAKE) -C $(COCOTB_LOCAL_DIFF_DIR) LOCAL_DIFF_VECTOR_CSV="$(abspath $(COCOTB_LOCAL_DIFF_VECTORS))" LOCAL_DIFF_COLUMN_ORIENTED=0
	$(MAKE) -C $(COCOTB_LOCAL_DIFF_DIR) LOCAL_DIFF_VECTOR_CSV="$(abspath $(COCOTB_LOCAL_DIFF_VECTORS))" LOCAL_DIFF_COLUMN_ORIENTED=1

hdl-project: hdl-params $(HDL_BUILD_DIR)
	bash -c 'HDL_SKIP_PARAM_GEN=1 PYTHON="$(PYTHON)" $(VIVADO_ENV) "$(VIVADO)" -mode batch -source "$(HDL_SCRIPT)" -tclargs "$(abspath $(HDL_BUILD_DIR))" $(VIVADO_PROJECT_NAME) $(VIVADO_PART) $(VIVADO_BOARD) $(HDL_SIM_TOP) project-only'

hdl-sim: hdl-params $(HDL_BUILD_DIR)
	bash -c 'HDL_SKIP_PARAM_GEN=1 PYTHON="$(PYTHON)" $(VIVADO_ENV) "$(VIVADO)" -mode batch -source "$(HDL_SCRIPT)" -tclargs "$(abspath $(HDL_BUILD_DIR))" $(VIVADO_PROJECT_NAME) $(VIVADO_PART) $(VIVADO_BOARD) $(HDL_SIM_TOP) simulate'

hdl-clean:
	rm -rf $(HDL_BUILD_DIR)

clean: cpp-clean hdl-clean
