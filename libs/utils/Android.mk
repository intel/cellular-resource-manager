LOCAL_PATH := $(call my-dir)

define all-h-files-under
$(patsubst ./%,%, $(shell cd $(LOCAL_PATH) ; find -L $(1) -name "*.h" -and -not -name ".*"))
endef

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := libcrm_utils

CRM_SRC := $(call all-c-files-under, src)

CRM_SHARED_LIBS_ANDROID_ONLY := libc libdl libcutils
CRM_STATIC_LIBS_HOST_ONLY := libcrm_host_test_utils
CRM_SHARED_LIBS := libtcs2

CRM_COPY_HEADERS_TO := telephony/libcrm/utils
CRM_COPY_HEADERS := $(call all-h-files-under, ../../inc/utils)

CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk

##############################################################
#      TESTU
##############################################################
ifeq ($(crm_testu), true)

include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := crm_test_utils

CRM_SRC := test/test_utils.c

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk

##############################################################
include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := crm_test_ipc

CRM_SRC := test/ipc_test.c

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk

##############################################################
include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := crm_test_thread

CRM_SRC := test/thread_test.c

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk

##############################################################
include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := crm_test_fsm

CRM_SRC := test/fsm_test.c

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk

##############################################################
include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := crm_test_process

CRM_SRC := test/process/process_test.c test/process/fake_plugin.c

CRM_REQUIRED_MODULES :=

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk

##############################################################
include $(LOCAL_PATH)/../../makefiles/crm_clear.mk
CRM_NAME := libcrm_test_fake_plugin_operation

CRM_SRC := test/process/fake_plugin_operation.c

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils

CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../makefiles/crm_c_make.mk


endif
