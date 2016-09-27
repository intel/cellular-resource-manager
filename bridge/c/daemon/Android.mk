LOCAL_PATH:= $(call my-dir)

##############################################################
#      EXECUTABLE
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := teljavabridged

CRM_SRC := $(call all-c-files-under, src)
CRM_INCS := $(LOCAL_PATH)/inc

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils
CRM_STATIC_LIBS_HOST_ONLY := libcrm_host_test_utils

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

##############################################################
#      TESTU
##############################################################
ifeq ($(crm_testu), true)

include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := test_teljavabridged

CRM_SRC := $(call all-c-files-under, test)

CRM_SHARED_LIBS := libcrm_utils libtel_java_bridge

CRM_DISABLE_ANDROID_TARGET := true
CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

endif
