// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MANAGED_MODE_MANAGED_USER_SERVICE_H_
#define CHROME_BROWSER_MANAGED_MODE_MANAGED_USER_SERVICE_H_

#include <set>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/prefs/pref_change_registrar.h"
#include "base/string16.h"
#include "chrome/browser/extensions/management_policy.h"
#include "chrome/browser/managed_mode/managed_mode_url_filter.h"
#include "chrome/browser/ui/webui/managed_user_passphrase_dialog.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/web_contents.h"

class Browser;
class GoogleServiceAuthError;
class ManagedModeURLFilter;
class ManagedModeSiteList;
class ManagedUserRegistrationService;
class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}

// This class handles all the information related to a given managed profile
// (e.g. the installed content packs, the default URL filtering behavior, or
// manual whitelist/blacklist overrides).
class ManagedUserService : public BrowserContextKeyedService,
                           public extensions::ManagementPolicy::Provider,
                           public content::NotificationObserver {
 public:
  typedef std::vector<string16> CategoryList;

  enum ManualBehavior {
    MANUAL_NONE = 0,
    MANUAL_ALLOW,
    MANUAL_BLOCK
  };

  explicit ManagedUserService(Profile* profile);
  virtual ~ManagedUserService();

  bool ProfileIsManaged() const;

  // Checks whether the given profile is managed without constructing a
  // ManagedUserService (which could lead to cyclic dependencies).
  static bool ProfileIsManaged(Profile* profile);

  // Returns the elevation state for specific WebContents.
  bool IsElevatedForWebContents(const content::WebContents* web_contents) const;

  static void RegisterUserPrefs(user_prefs::PrefRegistrySyncable* registry);

  // Returns whether managed users are enabled by Finch or the command line
  // flag.
  static bool AreManagedUsersEnabled();

  // Returns the URL filter for the IO thread, for filtering network requests
  // (in ManagedModeResourceThrottle).
  scoped_refptr<const ManagedModeURLFilter> GetURLFilterForIOThread();

  // Returns the URL filter for the UI thread, for filtering navigations and
  // classifying sites in the history view.
  ManagedModeURLFilter* GetURLFilterForUIThread();

  // Returns the URL's category, obtained from the installed content packs.
  int GetCategory(const GURL& url);

  // Returns the list of all known human-readable category names, sorted by ID
  // number. Called in the critical path of drawing the history UI, so needs to
  // be fast.
  void GetCategoryNames(CategoryList* list);

  // Returns the email address of the custodian.
  std::string GetCustodianEmailAddress() const;

  // These methods allow querying and modifying the manual filtering behavior.
  // The manual behavior is set by the user and overrides all other settings
  // (whitelists or the default behavior).

  // Returns the manual behavior for the given host.
  ManualBehavior GetManualBehaviorForHost(const std::string& hostname);

  // Sets the manual behavior for the given host.
  void SetManualBehaviorForHosts(const std::vector<std::string>& hostnames,
                                 ManualBehavior behavior);

  // Adds an access request for the given URL. The requests are stored using
  // a prefix followed by a URIEncoded version of the URL. Each entry contains
  // a dictionary which currently has the timestamp of the request in it.
  void AddAccessRequest(const GURL& url);

  // Returns the manual behavior for the given URL.
  ManualBehavior GetManualBehaviorForURL(const GURL& url);

  // Sets the manual behavior for the given URL.
  void SetManualBehaviorForURLs(const std::vector<GURL>& url,
                                ManualBehavior behavior);

  // Returns all URLS on the given host that have exceptions.
  void GetManualExceptionsForHost(const std::string& host,
                                  std::vector<GURL>* urls);

  // Checks if the passphrase dialog can be skipped (the profile is already in
  // elevated state for the given WebContents or the passphrase is empty).
  bool CanSkipPassphraseDialog(const content::WebContents* web_contents) const;

  // Handles the request to authorize as the custodian of the managed user.
  void RequestAuthorization(content::WebContents* web_contents,
                            const PassphraseCheckedCallback& callback);

  // Initializes this object. This method does nothing if the profile is not
  // managed.
  void Init();

  // Marks the profile as managed and initializes it.
  void InitForTesting();

  // Initializes this profile for syncing, using the provided |token| to
  // authenticate requests.
  void InitSync(const std::string& token);

  // Convenience method that registers this managed user with
  // |registration_service| and initializes sync with the returned token.
  // Note that |registration_service| should belong to the custodian's profile,
  // not this one.
  void RegisterAndInitSync(
      ManagedUserRegistrationService* registration_service);

  // Returns a pseudo-email address for systems that expect well-formed email
  // addresses (like Sync), even though we're not signed in.
  static const char* GetManagedUserPseudoEmail();

  void set_elevated_for_testing(bool skip) {
    elevated_for_testing_ = skip;
  }

  // extensions::ManagementPolicy::Provider implementation:
  virtual std::string GetDebugPolicyProviderName() const OVERRIDE;
  virtual bool UserMayLoad(const extensions::Extension* extension,
                           string16* error) const OVERRIDE;
  virtual bool UserMayModifySettings(const extensions::Extension* extension,
                                     string16* error) const OVERRIDE;

  // content::NotificationObserver implementation:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 private:
  friend class ManagedUserServiceExtensionTest;

  // A bridge from ManagedMode (which lives on the UI thread) to the
  // ManagedModeURLFilters, one of which lives on the IO thread. This class
  // mediates access to them and makes sure they are kept in sync.
  class URLFilterContext {
   public:
    URLFilterContext();
    ~URLFilterContext();

    ManagedModeURLFilter* ui_url_filter() const;
    ManagedModeURLFilter* io_url_filter() const;

    void SetDefaultFilteringBehavior(
        ManagedModeURLFilter::FilteringBehavior behavior);
    void LoadWhitelists(ScopedVector<ManagedModeSiteList> site_lists);
    void SetManualHosts(scoped_ptr<std::map<std::string, bool> > host_map);
    void SetManualURLs(scoped_ptr<std::map<GURL, bool> > url_map);

   private:
    // ManagedModeURLFilter is refcounted because the IO thread filter is used
    // both by ProfileImplIOData and OffTheRecordProfileIOData (to filter
    // network requests), so they both keep a reference to it.
    // Clients should not keep references to the UI thread filter, however
    // (the filter will live as long as the profile lives, and afterwards it
    // should not be used anymore either).
    scoped_refptr<ManagedModeURLFilter> ui_url_filter_;
    scoped_refptr<ManagedModeURLFilter> io_url_filter_;

    DISALLOW_COPY_AND_ASSIGN(URLFilterContext);
  };

  void OnManagedUserRegistered(const GoogleServiceAuthError& auth_error,
                               const std::string& token);

  // Internal implementation for ExtensionManagementPolicy::Delegate methods.
  // If |error| is not NULL, it will be filled with an error message if the
  // requested extension action (install, modify status, etc.) is not permitted.
  bool ExtensionManagementPolicyImpl(const std::string& extension_id,
                                     string16* error) const;

  // Returns a list of all installed and enabled site lists in the current
  // managed profile.
  ScopedVector<ManagedModeSiteList> GetActiveSiteLists();

  void OnDefaultFilteringBehaviorChanged();

  void UpdateSiteLists();

  // Updates the manual overrides for hosts in the URL filters when the
  // corresponding preference is changed.
  void UpdateManualHosts();

  // Updates the manual overrides for URLs in the URL filters when the
  // corresponding preference is changed.
  void UpdateManualURLs();

  // Returns if the passphrase to authorize as the custodian is empty.
  bool IsPassphraseEmpty() const;

  base::WeakPtrFactory<ManagedUserService> weak_ptr_factory_;

  // Owns us via the BrowserContextKeyedService mechanism.
  Profile* profile_;

  content::NotificationRegistrar registrar_;
  PrefChangeRegistrar pref_change_registrar_;

  // Stores the extension ids of the extensions which currently can be modified
  // by the managed user.
  std::set<std::string> elevated_for_extensions_;

  // Sets a profile in elevated state for testing if set to true.
  bool elevated_for_testing_;

  URLFilterContext url_filter_context_;
};

#endif  // CHROME_BROWSER_MANAGED_MODE_MANAGED_USER_SERVICE_H_
