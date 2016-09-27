CRM_PATH := $(call my-dir)/..

# Extract commit id
COMMIT_ID := $(shell git --git-dir=$(CRM_PATH)/.git \
        --work-tree=$(call my-dir) log --oneline -n1 \
        | sed -e 's:\s\{1,\}:\\ :g' -e 's:["&{}()]::g' \
        -e "s:'::g")

CRM_DEFAULT_CFLAGS := -Wall -Wvla -Wextra -Werror -pthread -DGIT_COMMIT_ID=\"$(COMMIT_ID)\"
ifneq ($(CRM_LANG), CPP)
CRM_DEFAULT_CFLAGS += -std=gnu99
endif

CRM_HOST_CFLAGS := -O0 -ggdb -pthread -DHOST_BUILD -DCLOCK_BOOTTIME=7
CRM_HOST_LDFLAGS := -lrt -ldl -rdynamic -lz

CRM_DEFAULT_INCS := $(CRM_PATH)/inc $(TARGET_OUT_HEADERS)/telephony

define copy_xml
include $(CLEAR_VARS)
LOCAL_MODULE := $(1)
LOCAL_SRC_FILES := $(2)$(strip $1)
LOCAL_MODULE_OWNER := intel
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := $(3)
LOCAL_IS_HOST_MODULE := $(4)
LOCAL_MODULE_RELATIVE_PATH := telephony/tcs/$(CRM_XML_MODULE)
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_PREBUILT)
endef

##############################################################
#      ANDROID TARGET
##############################################################
ifneq ($(CRM_DISABLE_ANDROID_TARGET), true)
include $(CLEAR_VARS)

LOCAL_MODULE := $(CRM_NAME)
LOCAL_MODULE_OWNER := intel
LOCAL_PROPRIETARY_MODULE := true

LOCAL_SRC_FILES := $(CRM_SRC)
LOCAL_C_INCLUDES := $(CRM_DEFAULT_INCS) $(CRM_INCS)

LOCAL_CFLAGS := $(CRM_DEFAULT_CFLAGS) $(CRM_CFLAGS)

LOCAL_SHARED_LIBRARIES := $(CRM_SHARED_LIBS_ANDROID_ONLY) $(CRM_SHARED_LIBS)
LOCAL_STATIC_LIBRARIES := $(CRM_STATIC_LIBS)

LOCAL_COPY_HEADERS := $(CRM_COPY_HEADERS)
LOCAL_COPY_HEADERS_TO := $(CRM_COPY_HEADERS_TO)

LOCAL_REQUIRED_MODULES := $(CRM_REQUIRED_MODULES)
ifneq ($(CRM_XML_FOLDER), )
LOCAL_REQUIRED_MODULES += CRM_XML_TARGET_$(CRM_NAME)
endif

include $(CRM_TARGET)
endif

##############################################################
#      HOST TARGET
##############################################################
ifneq ($(CRM_DISABLE_HOST_TARGET), true)
ifeq ($(crm_host), true)
include $(CLEAR_VARS)

#ENABLE TCS HOST
tcs_host := true

LOCAL_MODULE := $(CRM_NAME)
LOCAL_MODULE_OWNER := intel
LOCAL_PROPRIETARY_MODULE := true

LOCAL_SRC_FILES := $(CRM_SRC)
LOCAL_C_INCLUDES := $(CRM_DEFAULT_INCS) $(CRM_INCS)
LOCAL_IMPORT_C_INCLUDE_DIRS_FROM_SHARED_LIBRARIES := $(CRM_IMPORT_INCLUDES_FROM_SHARED_LIBRARIES_HOST_ONLY)

LOCAL_CFLAGS := $(CRM_DEFAULT_CFLAGS) $(CRM_HOST_CFLAGS) $(CRM_CFLAGS)
LOCAL_LDFLAGS := $(CRM_HOST_LDFLAGS)

LOCAL_SHARED_LIBRARIES := $(CRM_SHARED_LIBS) $(CRM_SHARED_LIBS_HOST_ONLY)
LOCAL_STATIC_LIBRARIES := $(CRM_STATIC_LIBS_HOST_ONLY) $(CRM_STATIC_LIBS)

# fix a dependency issue. This is a W/A
ifeq ($(findstring libcrm_host_test_utils, $(LOCAL_STATIC_LIBRARIES)), libcrm_host_test_utils)
    LOCAL_SHARED_LIBRARIES += libtcs2
endif

LOCAL_REQUIRED_MODULES := $(CRM_REQUIRED_MODULES) CRM_XML_HOST_$(CRM_NAME)
ifneq ($(CRM_XML_FOLDER), )
LOCAL_REQUIRED_MODULES += CRM_XML_HOST_$(CRM_NAME)
endif

LOCAL_COPY_HEADERS := $(CRM_COPY_HEADERS)
LOCAL_COPY_HEADERS_TO := $(CRM_COPY_HEADERS_TO)

ifeq ($(CRM_TARGET),$(BUILD_STATIC_LIBRARY))
    CRM_TARGET_HOST := $(BUILD_HOST_STATIC_LIBRARY)
else ifeq ($(CRM_TARGET),$(BUILD_SHARED_LIBRARY))
    CRM_TARGET_HOST := $(BUILD_HOST_SHARED_LIBRARY)
else ifeq ($(CRM_TARGET),$(BUILD_EXECUTABLE))
    CRM_TARGET_HOST := $(BUILD_HOST_EXECUTABLE)
endif

include $(CRM_TARGET_HOST)
endif
endif

#############################################################
#       XML files
#############################################################
XMLS_TARGET := $(notdir $(wildcard $(CRM_XML_FOLDER)/target/*.xml))
$(foreach xml, $(XMLS_TARGET), $(eval $(call copy_xml, $(xml), $(subst $(LOCAL_PATH)/,,$(CRM_XML_FOLDER)/target/), ETC, )))

XMLS_HOST := $(notdir $(wildcard $(CRM_XML_FOLDER)/host/*.xml))
$(foreach xml, $(XMLS_HOST), $(eval $(call copy_xml, $(xml), $(subst $(LOCAL_PATH)/,,$(CRM_XML_FOLDER)/host/), debug, true)))

include $(CLEAR_VARS)
LOCAL_MODULE := CRM_XML_TARGET_$(CRM_NAME)
LOCAL_MODULE_TAGS := optional
CUSTOM_TARGET := ETC
LOCAL_REQUIRED_MODULES := $(XMLS_TARGET)
include $(BUILD_PHONY_PACKAGE)

include $(CLEAR_VARS)
LOCAL_MODULE := CRM_XML_HOST_$(CRM_NAME)
LOCAL_MODULE_TAGS := optional
CUSTOM_TARGET := ETC
LOCAL_REQUIRED_MODULES := $(XMLS_HOST)
include $(BUILD_PHONY_PACKAGE)
