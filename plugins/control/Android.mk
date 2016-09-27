LOCAL_PATH := $(call my-dir)

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := libcrm_control

CRM_SRC := $(call all-c-files-under, src)

CRM_REQUIRED_MODULES := libmdmcli

CRM_SHARED_LIBS_ANDROID_ONLY := libc libdl
CRM_SHARED_LIBS := libcrm_utils libtcs2 libtel_java_bridge

CRM_XML_FOLDER := $(LOCAL_PATH)/xml

CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk

##############################################################
#      TESTU
##############################################################
ifeq ($(crm_testu), true)

include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := crm_test_control

CRM_SRC := test/control_test.c test/stub_client_abstraction.c
CRM_INCS := $(LOCAL_PATH)/src

CRM_REQUIRED_MODULES := libmdmcli

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_control libcrm_hal_stub libcrm_fw_upload_stub libcrm_utils
CRM_STATIC_LIBS_HOST_ONLY := libcrm_host_test_utils

CRM_DISABLE_ANDROID_TARGET := true
CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk

##############################################################
include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := crm_test_watchdog

CRM_SRC := test/watchdog_test.c src/watchdog.c
CRM_INCS := $(LOCAL_PATH)/src

CRM_REQUIRED_MODULES := libmdmcli

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils libcrm_wakelock_stub
CRM_STATIC_LIBS_HOST_ONLY := libcrm_host_test_utils

CRM_DISABLE_ANDROID_TARGET := true
CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk

endif
