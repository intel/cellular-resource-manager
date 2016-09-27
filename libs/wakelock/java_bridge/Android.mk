LOCAL_PATH := $(call my-dir)

##############################################################
#      LIBRARY
##############################################################
include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := libcrm_wakelock_java

CRM_SRC := $(call all-c-files-under, src)

CRM_SHARED_LIBS_ANDROID_ONLY := libc
CRM_SHARED_LIBS := libcrm_utils libtel_java_bridge

CRM_TARGET := $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

##############################################################
#      TESTU
##############################################################
ifeq ($(crm_testu), true)

include $(LOCAL_PATH)/../../../makefiles/crm_clear.mk
CRM_NAME := crm_test_wakelock_java

CRM_SRC := $(call all-c-files-under, test)

# @TODO: if this dependency is declared, an external component (shill-test-rpc-proxy)
# doesn't build. The link between this dependency and this build error has not been
# found. Maybe this was due to an instability in the mainline?
#CRM_REQUIRED_MODULES := teljavabridged

CRM_SHARED_LIBS_ANDROID_ONLY := libc libcrm_wakelock_java
CRM_SHARED_LIBS := libcrm_utils
CRM_SHARED_LIBS_HOST_ONLY := libcrm_wakelock_java

CRM_TARGET := $(BUILD_EXECUTABLE)
include $(LOCAL_PATH)/../../../makefiles/crm_c_make.mk

endif
