
CONFIG_FILE ?= Makefile.config

ifneq (,$(wildcard $(CONFIG_FILE)))
include $(CONFIG_FILE)
endif

SHELL := /bin/bash

VIVADO_ENV := source "$(SETTINGS_SCRIPT)" &&

.PHONY: help cpp cpp-configure cpp-build cpp-test cpp-clean hdl-project hdl-sim hdl-clean clean

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

hdl-project: $(HDL_BUILD_DIR)
	bash -c '$(VIVADO_ENV) "$(VIVADO)" -mode batch -source "$(HDL_SCRIPT)" -tclargs "$(abspath $(HDL_BUILD_DIR))" $(VIVADO_PROJECT_NAME) $(VIVADO_PART) $(VIVADO_BOARD) $(HDL_SIM_TOP) project-only'

hdl-sim: $(HDL_BUILD_DIR)
	bash -c '$(VIVADO_ENV) "$(VIVADO)" -mode batch -source "$(HDL_SCRIPT)" -tclargs "$(abspath $(HDL_BUILD_DIR))" $(VIVADO_PROJECT_NAME) $(VIVADO_PART) $(VIVADO_BOARD) $(HDL_SIM_TOP) simulate'

hdl-clean:
	rm -rf $(HDL_BUILD_DIR)

clean: cpp-clean hdl-clean
