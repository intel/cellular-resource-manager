LOCAL_PATH := $(call my-dir)

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := libcrm_ifwd

CRM_SRC := $(call all-c-files-under, src)
CRM_INCS := $(TARGET_OUT_HEADERS)/telephony/flsTool_interfaces external/zlib

CRM_SHARED_LIBS_ANDROID_ONLY := libc libDownloadTool libz
CRM_SHARED_LIBS := libcrm_utils

CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk
