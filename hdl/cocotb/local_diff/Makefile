TOPLEVEL_LANG ?= vhdl
SIM ?= ghdl

VHDL_SOURCES += $(PWD)/../../src/common.vhd
VHDL_SOURCES += $(PWD)/../../src/local_diff.vhd

TOPLEVEL ?= local_diff
MODULE ?= test_local_diff

COL_ORIENTED ?= 0
NZ ?= 10
D ?= 16

GHDL_ARGS += --std=08
GHDL_ARGS += -gCOL_ORIENTED=$(if $(filter 0,$(COL_ORIENTED)),false,true)
GHDL_ARGS += -gNZ=$(NZ)
GHDL_ARGS += -gD=$(D)

include $(shell cocotb-config --makefiles)/Makefile.sim
