LOCAL_PATH := $(call my-dir)

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := libcrm_fw_upload_pcie

CRM_SRC := src/fw_upload.c

CRM_REQUIRED_MODULES := libcrm_fw_upload_pcie_process

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils libcrm_ifwd

CRM_XML_FOLDER := $(LOCAL_PATH)/xml

CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := libcrm_fw_upload_pcie_process

CRM_SRC := src/fw_upload_process.c

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils libcrm_ifwd

CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

##############################################################
#      TESTU
##############################################################
ifeq ($(crm_testu), true)

include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := crm_test_fw_upload_pcie

CRM_SRC := test/fw_upload_test.c

CRM_SHARED_LIBS := libcrm_fw_upload_pcie libcrm_utils
CRM_STATIC_LIBS_HOST_ONLY := libcrm_host_test_utils

CRM_DISABLE_ANDROID_TARGET := true
CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

endif
