// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_EXTENSIONS_API_EXTENSION_ACTION_ACTION_INFO_H_
#define CHROME_COMMON_EXTENSIONS_API_EXTENSION_ACTION_ACTION_INFO_H_

#include <string>

#include "base/string16.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "googleurl/src/gurl.h"

namespace base {
class DictionaryValue;
}

namespace extensions {

class Extension;

struct ActionInfo {
  ActionInfo();
  ~ActionInfo();

  // The types of extension actions.
  enum Type {
    TYPE_BROWSER,
    TYPE_PAGE,
    TYPE_SCRIPT_BADGE,
    TYPE_SYSTEM_INDICATOR,
  };

  // Loads an ActionInfo from the given DictionaryValue.
  static scoped_ptr<ActionInfo> Load(const Extension* extension,
                                     const base::DictionaryValue* dict,
                                     string16* error);

  // Returns the extension's browser action, if any.
  static const ActionInfo* GetBrowserActionInfo(const Extension* extension);

  // Returns the extension's page action, if any.
  static const ActionInfo* GetPageActionInfo(const Extension* extension);

  // Returns the extension's script badge.
  static const ActionInfo* GetScriptBadgeInfo(const Extension* etxension);

  // Returns the extension's page launcher.
  static const ActionInfo* GetPageLauncherInfo(const Extension* extension);

  // Returns the extension's system indicator, if any.
  static const ActionInfo* GetSystemIndicatorInfo(const Extension* extension);

  // Sets the extension's browser action. |extension| takes ownership of |info|.
  static void SetBrowserActionInfo(Extension* extension, ActionInfo* info);

  // Sets the extension's page action. |extension| takes ownership of |info|.
  static void SetPageActionInfo(Extension* extension, ActionInfo* info);

  // Sets the extension's script badge. |extension| takes ownership of |info|.
  static void SetScriptBadgeInfo(Extension* extension, ActionInfo* info);

  // Sets the extension's page launcher. |extension| takes ownership of |info|.
  static void SetPageLauncherInfo(Extension* extension, ActionInfo* info);

  // Sets the extension's system indicator. |extension| takes ownership of
  // |info|.
  static void SetSystemIndicatorInfo(Extension* extension, ActionInfo* info);

  // Returns true if the extension needs a verbose install message because
  // of its page action.
  static bool IsVerboseInstallMessage(const Extension* extension);

  // Empty implies the key wasn't present.
  ExtensionIconSet default_icon;
  std::string default_title;
  GURL default_popup_url;
  // action id -- only used with legacy page actions API.
  std::string id;
};

}  // namespace extensions

#endif  // CHROME_COMMON_EXTENSIONS_API_EXTENSION_ACTION_ACTION_INFO_H_
