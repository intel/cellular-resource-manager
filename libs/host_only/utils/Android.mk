LOCAL_PATH:= $(call my-dir)

##############################################################
#      LIBRARY
##############################################################
ifeq ($(crm_host), true)
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := libcrm_host_test_utils

CRM_SRC := $(call all-c-files-under, src)

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils libtcs2

CRM_XML_FOLDER := $(LOCAL_PATH)/xml/config
CRM_XML_MODULE := "config"

CRM_REQUIRED_MODULES := streamline_host_xml nvm_host_xml

CRM_TARGET := $(BUILD_STATIC_LIBRARY)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := streamline_host_xml

CRM_XML_FOLDER := $(LOCAL_PATH)/xml/streamline
CRM_XML_MODULE := "streamline"

CRM_DISABLE_ANDROID_TARGET := true
CRM_TARGET := $(BUILD_PHONY_PACKAGE)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := nvm_host_xml

CRM_XML_FOLDER := $(LOCAL_PATH)/xml/nvm
CRM_XML_MODULE := "nvm"

CRM_DISABLE_ANDROID_TARGET := true
CRM_TARGET := $(BUILD_PHONY_PACKAGE)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

endif
