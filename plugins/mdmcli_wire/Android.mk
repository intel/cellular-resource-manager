LOCAL_PATH:= $(call my-dir)

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := libcrm_mdmcli_wire
CRM_INCS := $(LOCAL_PATH)/inc

CRM_SRC := $(call all-c-files-under, src)
CRM_REQUIRED_MODULES := libmdmcli

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils

CRM_TARGET := $(BUILD_STATIC_LIBRARY)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk

##############################################################
#      TESTU
##############################################################
include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := crm_test_mdmcli_wire

CRM_SRC := $(call all-c-files-under, test)
CRM_INCS := $(LOCAL_PATH)/inc

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils
CRM_STATIC_LIBS := libcrm_mdmcli_wire

CRM_DISABLE_ANDROID_TARGET := true
CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk
