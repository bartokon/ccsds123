
CONFIG_FILE ?= Makefile.config

ifneq (,$(wildcard $(CONFIG_FILE)))
include $(CONFIG_FILE)
endif

SHELL := /bin/bash

PYTHON ?= python3
HDL_CONFIG ?= tools/conf.json
HDL_PARAM_SCRIPT ?= tools/gen_impl_params.py
HDL_PARAM_DIR ?= src/hdl/tb
HDL_PARAM_FILES := $(HDL_PARAM_DIR)/impl_params.v $(HDL_PARAM_DIR)/synth_params.vhd

HDL_CONFIG_FIELDS := $(shell $(PYTHON) -c "import json; cfg=json.load(open('$(HDL_CONFIG)')); img=cfg['images'][0]; params=cfg['parameters']; depth=int(params['D']); sample_bytes=(depth + 7) // 8; print(f\"{int(img['NX'])} {int(img['NY'])} {int(img['NZ'])} {depth} {int(params.get('PIPELINES',1))} {sample_bytes} {int(img['NX']) * int(img['NY']) * int(img['NZ']) * sample_bytes}\")")
HDL_NX := $(word 1,$(HDL_CONFIG_FIELDS))
HDL_NY := $(word 2,$(HDL_CONFIG_FIELDS))
HDL_NZ := $(word 3,$(HDL_CONFIG_FIELDS))
HDL_DEPTH := $(word 4,$(HDL_CONFIG_FIELDS))
HDL_PIPELINES := $(word 5,$(HDL_CONFIG_FIELDS))
HDL_SAMPLE_BYTES := $(word 6,$(HDL_CONFIG_FIELDS))
HDL_INPUT_BYTES := $(word 7,$(HDL_CONFIG_FIELDS))

HDL_STIMULUS_DIR ?= build/vectors
HDL_OUTPUT_DIR ?= $(HDL_STIMULUS_DIR)/hdl
HDL_CPP_DIR ?= $(HDL_STIMULUS_DIR)/cpp
HDL_TEST_INPUT ?= $(HDL_STIMULUS_DIR)/gradient_u16le.bsq
HDL_CPP_CONTAINER ?= $(HDL_CPP_DIR)/bitstream.c123
HDL_CPP_PAYLOAD ?= $(HDL_CPP_DIR)/bitstream_payload.bin
HDL_HDL_PAYLOAD ?= $(HDL_OUTPUT_DIR)/out.bin

VIVADO_ENV := source "$(SETTINGS_SCRIPT)" &&

.PHONY: help cpp cpp-configure cpp-build cpp-test cpp-clean hdl-params hdl-project hdl-sim hdl-clean compare clean

help:
	@echo "Available targets:"
	@echo "  make cpp            - Configure, build, and test the C++ sources."
	@echo "  make cpp-build      - Build the C++ executables without running tests."
	@echo "  make cpp-test       - Run the C++ unit tests (builds first)."
	@echo "  make hdl-project    - Generate the Vivado project structure."
	@echo "  make hdl-sim        - Create the Vivado project and run behavioral simulation."
	@echo "  make compare        - Run HDL simulation and compare payload bits against the C++ reference."
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

$(HDL_STIMULUS_DIR):
	@mkdir -p $@

$(HDL_OUTPUT_DIR):
	@mkdir -p $@

$(HDL_CPP_DIR):
	@mkdir -p $@

$(HDL_TEST_INPUT): tools/generate_gradient.py | $(HDL_STIMULUS_DIR)
	$(PYTHON) tools/generate_gradient.py --nx $(HDL_NX) --ny $(HDL_NY) --nz $(HDL_NZ) --output $@

hdl-params:
	@mkdir -p $(HDL_PARAM_DIR)
	$(PYTHON) $(HDL_PARAM_SCRIPT) $(HDL_CONFIG)

hdl-project: hdl-params $(HDL_BUILD_DIR)
	bash -c 'HDL_SKIP_PARAM_GEN=1 PYTHON="$(PYTHON)" $(VIVADO_ENV) "$(VIVADO)" -mode batch -source "$(HDL_SCRIPT)" -tclargs "$(abspath $(HDL_BUILD_DIR))" $(VIVADO_PROJECT_NAME) $(VIVADO_PART) $(VIVADO_BOARD) $(HDL_SIM_TOP) project-only'

hdl-sim: hdl-params $(HDL_BUILD_DIR) $(HDL_TEST_INPUT) | $(HDL_OUTPUT_DIR)
	HDL_SKIP_PARAM_GEN=1 PYTHON="$(PYTHON)" HDL_INPUT_FILE="$(abspath $(HDL_TEST_INPUT))" HDL_OUTPUT_DIR="$(abspath $(HDL_OUTPUT_DIR))" \
	bash -c '$(VIVADO_ENV) "$(VIVADO)" -mode batch -source "$(HDL_SCRIPT)" -tclargs "$(abspath $(HDL_BUILD_DIR))" $(VIVADO_PROJECT_NAME) $(VIVADO_PART) $(VIVADO_BOARD) $(HDL_SIM_TOP) simulate'

compare: cpp-build $(HDL_TEST_INPUT) | $(HDL_CPP_DIR)
	$(CPP_BUILD_DIR)/src/cpp/ccsds123_encode -i "$(abspath $(HDL_TEST_INPUT))" -o "$(abspath $(HDL_CPP_CONTAINER))" -nx $(HDL_NX) -ny $(HDL_NY) -nz $(HDL_NZ) -d $(HDL_DEPTH)
	$(MAKE) hdl-sim
	$(PYTHON) tools/compare_bitstreams.py --container "$(abspath $(HDL_CPP_CONTAINER))" --hdl-payload "$(abspath $(HDL_HDL_PAYLOAD))" --payload-output "$(abspath $(HDL_CPP_PAYLOAD))" --input-bytes $(HDL_INPUT_BYTES) --input-file "$(abspath $(HDL_TEST_INPUT))" --decoder "$(abspath $(CPP_BUILD_DIR))/src/cpp/ccsds123_decode"

hdl-clean:
	rm -rf $(HDL_BUILD_DIR)

logs-clean:
	rm -rf vivado*.jou
	rm -rf vivado*.log
	rm -rf xvlog.pb

clean: logs-clean cpp-clean hdl-clean
