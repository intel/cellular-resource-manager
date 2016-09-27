LOCAL_PATH:= $(call my-dir)

##############################################################
#      TEST
##############################################################
include $(LOCAL_PATH)/../../../../makefiles/crm_clear.mk
CRM_NAME := crm_test_sanity_sofia

CRM_SRC := $(call all-c-files-under, src)

CRM_REQUIRED_MODULES := libmdmcli
CRM_SHARED_LIBS_ANDROID_ONLY := libc libdl liblog
CRM_SHARED_LIBS := libcrm_utils
CRM_DISABLE_HOST_TARGET := true

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../../../makefiles/crm_c_make.mk
