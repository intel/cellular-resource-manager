LOCAL_PATH := $(call my-dir)

##############################################################
#      TESTU
##############################################################
ifeq ($(crm_testu), true)

include $(LOCAL_PATH)/../../../../makefiles/crm_clear.mk
CRM_NAME := crm_test_stub_modem_sofia

CRM_SRC := $(call all-c-files-under, .)

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../../../makefiles/crm_c_make.mk

endif
