LOCAL_PATH := $(call my-dir)

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := libcrm_dump_pcie

CRM_SRC := src/dump.c

CRM_REQUIRED_MODULES := libcrm_dump_pcie_process

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils libcrm_ifwd

CRM_XML_FOLDER := $(LOCAL_PATH)/xml

CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := libcrm_dump_pcie_process

CRM_SRC := src/dump_process.c

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils libcrm_ifwd

CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk
