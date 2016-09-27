LOCAL_PATH:= $(call my-dir)

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := libtel_java_bridge

CRM_SRC := $(call all-c-files-under, src)
CRM_INCS := $(LOCAL_PATH)/inc $(LOCAL_PATH)/../daemon/inc

CRM_SHARED_LIBS_ANDROID_ONLY := libc libcutils
CRM_SHARED_LIBS := libcrm_utils

CRM_COPY_HEADERS := inc/tel_java_bridge.h
CRM_COPY_HEADERS_TO := telephony/teljavabridge

CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk
