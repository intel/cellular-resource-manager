LOCAL_PATH:= $(call my-dir)

##############################################################
#      TESTU
##############################################################
ifeq ($(crm_testu), true)

include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := crm_random_client

CRM_SRC := $(call all-c-files-under, src)

CRM_REQUIRED_MODULES := libtcs2 libmdmcli

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_mdmcli libcrm_utils
CRM_STATIC_LIBS := crm_client_factory

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

endif
