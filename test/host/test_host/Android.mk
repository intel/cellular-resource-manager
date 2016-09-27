LOCAL_PATH:= $(call my-dir)

##############################################################
#      TESTU
##############################################################
ifeq ($(crm_testu), true)

include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := crm_test_host

CRM_SRC := $(call all-c-files-under, .)

CRM_REQUIRED_MODULES := libmdmcli crm_test_stub_modem_sofia crm

CRM_SHARED_LIBS := libcrm_mdmcli libcrm_utils libtcs2
CRM_STATIC_LIBS_HOST_ONLY := libcrm_host_test_utils

CRM_DISABLE_ANDROID_TARGET := true
CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

endif
