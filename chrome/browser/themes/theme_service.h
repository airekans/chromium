// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_THEMES_THEME_SERVICE_H_
#define CHROME_BROWSER_THEMES_THEME_SERVICE_H_

#include <map>
#include <set>
#include <string>
#include <utility>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "ui/base/theme_provider.h"

class BrowserThemePack;
class ThemeServiceTest;
class ThemeSyncableService;
class Profile;

namespace base {
class FilePath;
}

namespace color_utils {
struct HSL;
}

namespace extensions {
class Extension;
}

namespace gfx {
class Image;
}

namespace ui {
class ResourceBundle;
}

#ifdef __OBJC__
@class NSString;
// Sent whenever the browser theme changes.  Object => NSValue wrapping the
// ThemeService that changed.
extern "C" NSString* const kBrowserThemeDidChangeNotification;
#endif  // __OBJC__

class ThemeService : public base::NonThreadSafe,
                     public ProfileKeyedService,
                     public ui::ThemeProvider {
 public:
  // Public constants used in ThemeService and its subclasses:
  static const char* kDefaultThemeID;

  ThemeService();
  virtual ~ThemeService();

  virtual void Init(Profile* profile);

  // Returns a cross platform image for an id.
  //
  // TODO(erg): Make this part of the ui::ThemeProvider and the main way to get
  // theme properties out of the theme provider since it's cross platform.
  virtual gfx::Image GetImageNamed(int id) const;

  // Overridden from ui::ThemeProvider:
  virtual gfx::ImageSkia* GetImageSkiaNamed(int id) const OVERRIDE;
  virtual SkColor GetColor(int id) const OVERRIDE;
  virtual bool GetDisplayProperty(int id, int* result) const OVERRIDE;
  virtual bool ShouldUseNativeFrame() const OVERRIDE;
  virtual bool HasCustomImage(int id) const OVERRIDE;
  virtual base::RefCountedMemory* GetRawData(
      int id,
      ui::ScaleFactor scale_factor) const OVERRIDE;
#if defined(OS_MACOSX)
  virtual NSImage* GetNSImageNamed(int id, bool allow_default) const OVERRIDE;
  virtual NSColor* GetNSImageColorNamed(int id,
                                        bool allow_default) const OVERRIDE;
  virtual NSColor* GetNSColor(int id, bool allow_default) const OVERRIDE;
  virtual NSColor* GetNSColorTint(int id, bool allow_default) const OVERRIDE;
  virtual NSGradient* GetNSGradient(int id) const OVERRIDE;
#elif defined(OS_POSIX) && !defined(TOOLKIT_VIEWS) && !defined(OS_ANDROID)
  // This mismatch between what this class defines and whether or not it
  // overrides ui::ThemeProvider is http://crbug.com/105040 .
  // GdkPixbufs returned by GetPixbufNamed and GetRTLEnabledPixbufNamed are
  // shared instances owned by the theme provider and should not be freed.
  virtual GdkPixbuf* GetRTLEnabledPixbufNamed(int id) const OVERRIDE;
#endif

  // Set the current theme to the theme defined in |extension|.
  // |extension| must already be added to this profile's
  // ExtensionService.
  virtual void SetTheme(const extensions::Extension* extension);

  // Reset the theme to default.
  virtual void UseDefaultTheme();

  // Set the current theme to the native theme. On some platforms, the native
  // theme is the default theme.
  virtual void SetNativeTheme();

  // Whether we're using the chrome default theme. Virtual so linux can check
  // if we're using the GTK theme.
  virtual bool UsingDefaultTheme() const;

  // Whether we're using the native theme (which may or may not be the
  // same as the default theme).
  virtual bool UsingNativeTheme() const;

  // Gets the id of the last installed theme. (The theme may have been further
  // locally customized.)
  virtual std::string GetThemeID() const;

  // This class needs to keep track of the number of theme infobars so that we
  // clean up unused themes.
  void OnInfobarDisplayed();

  // Decrements the number of theme infobars. If the last infobar has been
  // destroyed, uninstalls all themes that aren't the currently selected.
  void OnInfobarDestroyed();

  // Remove preference values for themes that are no longer in use.
  void RemoveUnusedThemes();

  // Returns the syncable service for syncing theme. The returned service is
  // owned by |this| object.
  virtual ThemeSyncableService* GetThemeSyncableService() const;

  // Save the images to be written to disk, mapping file path to id.
  typedef std::map<base::FilePath, int> ImagesDiskCache;

 protected:
  // Get the specified tint - |id| is one of the TINT_* enum values.
  color_utils::HSL GetTint(int id) const;

  // Clears all the override fields and saves the dictionary.
  virtual void ClearAllThemeData();

  // Load theme data from preferences.
  virtual void LoadThemePrefs();

  // Let all the browser views know that themes have changed.
  virtual void NotifyThemeChanged();

#if defined(OS_MACOSX)
  // Let all the browser views know that themes have changed in a platform way.
  virtual void NotifyPlatformThemeChanged();
#endif  // OS_MACOSX

  // Clears the platform-specific caches. Do not call directly; it's called
  // from ClearCaches().
  virtual void FreePlatformCaches();

  Profile* profile() { return profile_; }

 private:
  friend class ThemeServiceTest;

  // Saves the filename of the cached theme pack.
  void SavePackName(const base::FilePath& pack_path);

  // Save the id of the last theme installed.
  void SaveThemeID(const std::string& id);

  // Implementation of SetTheme() (and the fallback from LoadThemePrefs() in
  // case we don't have a theme pack).
  void BuildFromExtension(const extensions::Extension* extension);

#if defined(TOOLKIT_GTK)
  // Loads an image and flips it horizontally if |rtl_enabled| is true.
  GdkPixbuf* GetPixbufImpl(int id, bool rtl_enabled) const;
#endif

#if defined(TOOLKIT_GTK)
  typedef std::map<int, GdkPixbuf*> GdkPixbufMap;
  mutable GdkPixbufMap gdk_pixbufs_;
#elif defined(OS_MACOSX)
  typedef std::map<int, NSImage*> NSImageMap;
  mutable NSImageMap nsimage_cache_;

  // The bool member of the pair is whether the color is a default color.
  typedef std::map<int, std::pair<NSColor*, bool> > NSColorMap;
  mutable NSColorMap nscolor_cache_;

  typedef std::map<int, NSGradient*> NSGradientMap;
  mutable NSGradientMap nsgradient_cache_;
#endif

  ui::ResourceBundle& rb_;
  Profile* profile_;

  scoped_refptr<BrowserThemePack> theme_pack_;

  // The number of infobars currently displayed.
  int number_of_infobars_;

  scoped_ptr<ThemeSyncableService> theme_syncable_service_;

  DISALLOW_COPY_AND_ASSIGN(ThemeService);
};

#endif  // CHROME_BROWSER_THEMES_THEME_SERVICE_H_
