LOCAL_PATH:= $(call my-dir)

##############################################################
#      BIN - test app
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := crm_test

CRM_SRC := $(call all-c-files-under, .)

CRM_REQUIRED_MODULES := libmdmcli

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_mdmcli libcrm_utils libtcs2

CRM_TARGET := $(BUILD_EXECUTABLE)
CRM_DISABLE_HOST_TARGET := true
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk
