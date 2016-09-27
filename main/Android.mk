LOCAL_PATH := $(call my-dir)

##############################################################
#      MAIN
##############################################################
include $(LOCAL_PATH)/../makefiles/crm_clear.mk
CRM_NAME := crm

CRM_SRC := $(call all-c-files-under, src)

CRM_SHARED_LIBS_ANDROID_ONLY := libc libdl
CRM_SHARED_LIBS := libcrm_utils libtcs2

CRM_XML_FOLDER := $(LOCAL_PATH)/xml

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../makefiles/crm_c_make.mk

##############################################################
CRM_GENERIC_PLUGINS := \
    crm \
    libcrm_utils \
    libcrm_client_abstraction \
    libcrm_control \
    libcrm_escalation \
    libcrm_wakelock \
    libcrm_fw_elector \
    libcrm_hal_stub \
    libcrm_customization \
    libcrm_mdmcli \
    libmdmcli_jni \
    mdm_fw_pkg

##############################################################
#      SOFIA rule
##############################################################
include $(CLEAR_VARS)
LOCAL_MODULE := crm_sofia
LOCAL_MODULE_TAGS := optional
LOCAL_REQUIRED_MODULES := \
    $(CRM_GENERIC_PLUGINS) \
    libcrm_fw_upload_sofia_vmodem \
    libcrm_dump_sofia \
    libcrm_hal_sofia \
    libcrm_fw_upload_stub

include $(BUILD_PHONY_PACKAGE)

##############################################################
#      PCIE rule
##############################################################
include $(CLEAR_VARS)
LOCAL_MODULE := crm_pcie
LOCAL_MODULE_TAGS := optional
LOCAL_REQUIRED_MODULES := \
    $(CRM_GENERIC_PLUGINS) \
    libcrm_hal_pcie \
    libcrm_fw_upload_pcie \
    libcrm_dump_pcie

include $(BUILD_PHONY_PACKAGE)


##############################################################
#      DEBUG rule (generic)
##############################################################
include $(CLEAR_VARS)
LOCAL_MODULE := crm_dbg
LOCAL_REQUIRED_MODULES := crm_test
ifneq ($(crm_disable_java_build), true)
LOCAL_REQUIRED_MODULES += ModemClientJavaTest
endif

include $(BUILD_PHONY_PACKAGE)
