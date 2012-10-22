# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This package provides the parts of the WebView java code which live in the
# Chromium tree. This is built into a static library so it can be used by the
# glue layer in the Android tree.

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := android_webview_java

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := $(call all-java-files-under, java/src)

# contentview and its dependencies
LOCAL_AIDL_INCLUDES := $(LOCAL_PATH)/../content/public/android/java/src
LOCAL_SRC_FILES += \
    $(call all-java-files-under, ../content/public/android/java/src) \
    ../content/public/android/java/src/org/chromium/content/common/ISandboxedProcessCallback.aidl \
    ../content/public/android/java/src/org/chromium/content/common/ISandboxedProcessService.aidl \
    $(call all-java-files-under, ../base/android/java/src) \
    $(call all-java-files-under, ../media/base/android/java/src) \
    $(call all-java-files-under, ../net/android/java/src) \
    $(call all-java-files-under, ../ui/android/java/src) \

# browser components
LOCAL_SRC_FILES += \
    $(call all-java-files-under, ../chrome/browser/component/web_contents_delegate_android/java/src) \
    $(call all-java-files-under, ../chrome/browser/component/navigation_interception/java/src) \

# TODO(mkosiba): Remove chromium_chrome dep once required browser
# components are in (replace it with contentview).
LOCAL_SRC_FILES += \
    $(call all-java-files-under, ../chrome/android/java/src) \

# This file is generated by net.gyp:net_errors_java
LOCAL_GENERATED_SOURCES := $(call intermediates-dir-for,GYP,shared)/net/template/NetError.java \

include $(BUILD_STATIC_JAVA_LIBRARY)


########################################################
# These packages are the resource paks used by webview.

include $(CLEAR_VARS)
LOCAL_MODULE := webviewchromium_res_chrome
LOCAL_MODULE_STEM := chrome
include $(LOCAL_PATH)/webview_pak.mk

include $(CLEAR_VARS)
LOCAL_MODULE := webviewchromium_res_resources
LOCAL_MODULE_STEM := resources
include $(LOCAL_PATH)/webview_pak.mk

include $(CLEAR_VARS)
LOCAL_MODULE := webviewchromium_res_chrome_100_percent
LOCAL_MODULE_STEM := chrome_100_percent
include $(LOCAL_PATH)/webview_pak.mk

# TODO(torne): add other locales (filtered by PRODUCT_LOCALES?)
include $(CLEAR_VARS)
LOCAL_MODULE := webviewchromium_res_en-US
LOCAL_MODULE_STEM := locales/en-US
include $(LOCAL_PATH)/webview_pak.mk
