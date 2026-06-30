include ../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

DIR := $(shell basename `pwd`)
BUILD := $(SDK_RTSMART_BUILD_DIR)/examples/pangofly
BIN_TEST := $(RTT_MPP_EXAMPLES_ELF_INSTALL_PATH)/pangofly_test.elf
BIN_WRITER := $(RTT_MPP_EXAMPLES_ELF_INSTALL_PATH)/pangofly_writer.elf
BIN_READER := $(RTT_MPP_EXAMPLES_ELF_INSTALL_PATH)/pangofly_reader.elf
BIN_NONPOD_WRITER := $(RTT_MPP_EXAMPLES_ELF_INSTALL_PATH)/pangofly_nonpod_writer.elf
BIN_NONPOD_READER := $(RTT_MPP_EXAMPLES_ELF_INSTALL_PATH)/pangofly_nonpod_reader.elf

OBJS_LIB := \
	$(BUILD)/segment.o \
	$(BUILD)/shm_conf.o \
	$(BUILD)/state.o \
	$(BUILD)/k230_shm.o \
	$(BUILD)/node.o \
	$(BUILD)/pangofly.o

OBJS_TEST := $(BUILD)/shm_example.o
OBJS_WRITER := $(BUILD)/writer.o
OBJS_READER := $(BUILD)/reader.o
OBJS_NONPOD_WRITER := $(BUILD)/nonpod_writer.o
OBJS_NONPOD_READER := $(BUILD)/nonpod_reader.o

DEPS := $(OBJS_LIB:.o=.d) $(OBJS_TEST:.o=.d) $(OBJS_WRITER:.o=.d) $(OBJS_READER:.o=.d) \
        $(OBJS_NONPOD_WRITER:.o=.d) $(OBJS_NONPOD_READER:.o=.d)

OBJ_DIRS := $(BUILD)

include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk

C_CXX_FLAGS := -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os
C_CXX_FLAGS += -I$(realpath ..) 
C_CXX_FLAGS += -I$(realpath ../idl) 
C_CXX_FLAGS += -I$(realpath ../idl/container) 
C_CXX_FLAGS += -I$(SDK_SRC_ROOT_DIR)/include 
C_CXX_FLAGS += -I$(SDK_RTSMART_SRC_DIR)/include 
C_CXX_FLAGS += -I$(SDK_RTSMART_SRC_DIR)/rtsmart/kernel/rt-thread/components/lwp 
C_CXX_FLAGS += -I$(SDK_RTSMART_SRC_DIR)/rtsmart/userapps/sdk/rt-thread/include 
C_CXX_FLAGS += -I$(SDK_RTSMART_SRC_DIR)/rtsmart/userapps 
C_CXX_FLAGS += -include "$(SDK_SRC_ROOT_DIR)/include/generated/autoconf.h"
C_CXX_FLAGS += -Wall -Wextra -ffunction-sections -fdata-sections -Wno-unused-variable
C_CXX_FLAGS += -DPANGOFLY_PLATFORM_K230

CXX_FLAGS := $(C_CXX_FLAGS) -std=c++20 

LDFLAGS := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static -Wl,--gc-sections
LDFLAGS += $(LIB_LDFLAGS)

.PHONY: all clean distclean

all: $(BIN_TEST) $(BIN_WRITER) $(BIN_READER) $(BIN_NONPOD_WRITER) $(BIN_NONPOD_READER)
	@$(ECHO) [BUILD] examples pangofly done.

$(BIN_TEST): $(OBJS_LIB) $(OBJS_TEST) | $(OBJ_DIRS)
	@echo "[LD] $@"
	$(Q)$(CXX) $(CXX_FLAGS) $^ $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

$(BIN_WRITER): $(OBJS_LIB) $(OBJS_WRITER) | $(OBJ_DIRS)
	@echo "[LD] $@"
	$(Q)$(CXX) $(CXX_FLAGS) $^ $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

$(BIN_READER): $(OBJS_LIB) $(OBJS_READER) | $(OBJ_DIRS)
	@echo "[LD] $@"
	$(Q)$(CXX) $(CXX_FLAGS) $^ $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

$(BUILD)/segment.o: ../core/transport/shm/segment.cc | $(OBJ_DIRS)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/shm_conf.o: ../core/transport/shm/shm_conf.cc | $(OBJ_DIRS)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/state.o: ../core/transport/shm/state.cc | $(OBJ_DIRS)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/k230_shm.o: ../core/transport/shm/k230_shm.cc | $(OBJ_DIRS)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/node.o: ../core/node/node.cc | $(OBJ_DIRS)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/pangofly.o: ../core/pangofly.cc | $(OBJ_DIRS)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/shm_example.o: ../examples/core/shm_example.cc | $(OBJ_DIRS)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/writer.o: ../examples/core/writer.cc | $(OBJ_DIRS)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/reader.o: ../examples/core/reader.cc | $(OBJ_DIRS)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/nonpod_writer.o: ../examples/core/nonpod_writer.cc | $(OBJ_DIRS)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/nonpod_reader.o: ../examples/core/nonpod_reader.cc | $(OBJ_DIRS)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BIN_NONPOD_WRITER): $(OBJS_LIB) $(OBJS_NONPOD_WRITER) | $(OBJ_DIRS)
	@echo "[LD] $@"
	$(Q)$(CXX) $(CXX_FLAGS) $^ $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

$(BIN_NONPOD_READER): $(OBJS_LIB) $(OBJS_NONPOD_READER) | $(OBJ_DIRS)
	@echo "[LD] $@"
	$(Q)$(CXX) $(CXX_FLAGS) $^ $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

$(OBJ_DIRS):
	$(Q)mkdir -p $@

clean:
	@rm -rf $(BIN_TEST) $(BIN_WRITER) $(BIN_READER) $(BIN_NONPOD_WRITER) $(BIN_NONPOD_READER)

distclean: clean
	@rm -rf $(BUILD)

-include $(DEPS)