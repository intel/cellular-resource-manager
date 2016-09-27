LOCAL_PATH := $(my-dir)

# Create "phony" package to build all Java Bridge components with a single target
include $(CLEAR_VARS)
LOCAL_MODULE := tel_java_bridge
LOCAL_REQUIRED_MODULES := \
    TelJavaBridge \
    teljavabridged \
    libtel_java_bridge

include $(BUILD_PHONY_PACKAGE)

include $(call first-makefiles-under,$(LOCAL_PATH))
