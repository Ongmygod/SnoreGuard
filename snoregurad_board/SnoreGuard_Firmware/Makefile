################################################################################
# SnoreGuard Firmware Makefile
# Combines: Bluetooth_LE_Hello_Sensor + DEEPCRAFT_Deploy_Model_Audio
################################################################################

MTB_TYPE=COMBINED
TARGET=APP_CY8CKIT-062S2-AI
APPNAME=snoreguard-firmware
TOOLCHAIN=GCC_ARM
CONFIG=Debug
VERBOSE=

MTB_SUPPORTED_TOOLCHAINS?=GCC_ARM

################################################################################
# Components: BLE stack + FreeRTOS + ML inference
################################################################################
COMPONENTS=FREERTOS WICED_BLE ML_TFLM CMSIS_DSP ML_INT8x8

DISABLE_COMPONENTS=

SOURCES=
INCLUDES=

# ARM math, TFLite static memory, BLE, and SnoreGuard debug logging
DEFINES=ARM_MATH_DSP ARM_MATH_LOOPUNROLL TF_LITE_STATIC_MEMORY \
        CY_RETARGET_IO_CONVERT_LF_TO_CRLF CY_RTOS_AWARE \
        SNOREGUARD_DEBUG_LOG USE_INTERNAL_FLASH

# hardfp for DSP/ML performance
VFP_SELECT=hardfp

CFLAGS=
CXXFLAGS=
ASFLAGS=
LDFLAGS=
LDLIBS=
LINKER_SCRIPT=
PREBUILD=
POSTBUILD=

################################################################################
# Paths
################################################################################
CY_APP_PATH=
CY_GETLIBS_SHARED_PATH=../
CY_GETLIBS_SHARED_NAME=mtb_shared

CY_WIN_HOME=$(subst \,/,$(USERPROFILE))
CY_TOOLS_PATHS ?= $(wildcard \
    $(CY_WIN_HOME)/ModusToolbox/tools_* \
    $(HOME)/ModusToolbox/tools_* \
    /Applications/ModusToolbox/tools_*)
CY_TOOLS_PATHS+=

CY_TOOLS_DIR=$(lastword $(sort $(wildcard $(CY_TOOLS_PATHS))))
ifeq ($(CY_TOOLS_DIR),)
$(error Unable to find any of the available CY_TOOLS_PATHS -- $(CY_TOOLS_PATHS))
endif
$(info Tools Directory: $(CY_TOOLS_DIR))

include $(CY_TOOLS_DIR)/make/start.mk
