// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/themes/theme_service_factory.h"

#include "base/logging.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/common/pref_names.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"
#include "components/user_prefs/pref_registry_syncable.h"

#if defined(TOOLKIT_GTK)
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#endif

// static
ThemeService* ThemeServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<ThemeService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
const extensions::Extension* ThemeServiceFactory::GetThemeForProfile(
    Profile* profile) {
  std::string id = GetForProfile(profile)->GetThemeID();
  if (id == ThemeService::kDefaultThemeID)
    return NULL;

  return profile->GetExtensionService()->GetExtensionById(id, false);
}

// static
ThemeServiceFactory* ThemeServiceFactory::GetInstance() {
  return Singleton<ThemeServiceFactory>::get();
}

ThemeServiceFactory::ThemeServiceFactory()
    : BrowserContextKeyedServiceFactory(
        "ThemeService",
        BrowserContextDependencyManager::GetInstance()) {}

ThemeServiceFactory::~ThemeServiceFactory() {}

BrowserContextKeyedService* ThemeServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  ThemeService* provider = NULL;
#if defined(TOOLKIT_GTK)
  provider = new GtkThemeService;
#else
  provider = new ThemeService;
#endif
  provider->Init(static_cast<Profile*>(profile));

  return provider;
}

void ThemeServiceFactory::RegisterUserPrefs(
    user_prefs::PrefRegistrySyncable* registry) {
#if defined(TOOLKIT_GTK)
  registry->RegisterBooleanPref(
      prefs::kUsesSystemTheme,
      GtkThemeService::DefaultUsesSystemTheme(),
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
#endif
  registry->RegisterFilePathPref(
      prefs::kCurrentThemePackFilename,
      base::FilePath(),
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterStringPref(
      prefs::kCurrentThemeID,
      ThemeService::kDefaultThemeID,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterDictionaryPref(
      prefs::kCurrentThemeImages,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterDictionaryPref(
      prefs::kCurrentThemeColors,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterDictionaryPref(
      prefs::kCurrentThemeTints,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterDictionaryPref(
      prefs::kCurrentThemeDisplayProperties,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

content::BrowserContext* ThemeServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

bool ThemeServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}
