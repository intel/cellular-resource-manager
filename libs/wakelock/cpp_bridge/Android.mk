LOCAL_PATH := $(call my-dir)

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := libcrm_wakelock

CRM_SRC := $(call all-cpp-files-under, src)

CRM_SHARED_LIBS_ANDROID_ONLY := libutils libbinder libpowermanager liblog
CRM_SHARED_LIBS := libcrm_utils

CRM_LANG := CPP
CRM_TARGET := $(BUILD_SHARED_LIBRARY)
CRM_DISABLE_HOST_TARGET := true
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := libcrm_wakelock_stub

CRM_SRC := src/CrmWakelockMux.cpp src/wakelock.cpp src/CrmWakelockServiceBase.cpp

CRM_SHARED_LIBS := libcrm_utils

CRM_LANG := CPP
CRM_TARGET := $(BUILD_SHARED_LIBRARY)
CRM_CFLAGS := -DSTUB_BUILD
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

##############################################################
#      TESTU
##############################################################
ifeq ($(crm_testu), true)

include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := crm_test_wakelock

CRM_SRC := $(call all-c-files-under, test)

CRM_SHARED_LIBS_ANDROID_ONLY := libc libcrm_wakelock
CRM_SHARED_LIBS := libcrm_utils
CRM_SHARED_LIBS_HOST_ONLY := libcrm_wakelock_stub

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

endif
