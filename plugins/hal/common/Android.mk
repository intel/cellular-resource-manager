LOCAL_PATH := $(call my-dir)

##############################################################
#      LIBRARY
##############################################################

# No library here (files are built in HAL libraries)

##############################################################
#      TESTU
##############################################################
ifeq ($(crm_testu), true)

include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := crm_test_hal_daemons

CRM_SRC := src/daemons.c test/test_daemons.c
CRM_INCS := $(LOCAL_PATH)/src

CRM_SHARED_LIBS := libcrm_utils
CRM_STATIC_LIBS_HOST_ONLY := libcrm_host_test_utils

CRM_DISABLE_ANDROID_TARGET := true
CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

endif
