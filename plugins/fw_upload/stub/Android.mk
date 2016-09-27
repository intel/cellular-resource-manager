LOCAL_PATH := $(call my-dir)

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := libcrm_fw_upload_stub

CRM_SRC := $(call all-c-files-under, src)

CRM_REQUIRED_MODULES := libmdmcli libtcs2

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils
CRM_STATIC_LIBS_HOST_ONLY := libcrm_host_test_utils

CRM_DISABLE_ANDROID_TARGET := true
CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

