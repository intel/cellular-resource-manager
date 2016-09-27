LOCAL_PATH := $(call my-dir)

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := libcrm_hal_sofia

CRM_SRC := $(call all-c-files-under, ../common/src) $(call all-c-files-under, src)
CRM_INCS := $(LOCAL_PATH)/../common/src $(LOCAL_PATH)/src

CRM_REQUIRED_MODULES := libmdmcli

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils libtcs2

CRM_XML_FOLDER := $(LOCAL_PATH)/xml

CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

##############################################################
#      TESTU
##############################################################
ifeq ($(crm_testu), true)

include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := crm_test_hal_sofia

CRM_SRC := $(call all-c-files-under, test)
CRM_INCS := $(LOCAL_PATH)/src

CRM_REQUIRED_MODULES := libmdmcli crm_test_stub_modem_sofia

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_STATIC_LIBS_HOST_ONLY := libcrm_host_test_utils
CRM_SHARED_LIBS := libcrm_hal_sofia libcrm_utils

CRM_DISABLE_ANDROID_TARGET := true
CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

endif
