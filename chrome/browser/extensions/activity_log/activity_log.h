// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_ACTIVITY_LOG_ACTIVITY_LOG_H_
#define CHROME_BROWSER_EXTENSIONS_ACTIVITY_LOG_ACTIVITY_LOG_H_

#include <map>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/hash_tables.h"
#include "base/memory/singleton.h"
#include "base/observer_list_threadsafe.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread.h"
#include "chrome/browser/extensions/activity_log/activity_actions.h"
#include "chrome/browser/extensions/activity_log/activity_database.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/dom_action_types.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"
#include "content/public/browser/browser_thread.h"

class Profile;
using content::BrowserThread;

namespace extensions {
class Extension;

// A utility for tracing interesting activity for each extension.
// It writes to an ActivityDatabase on a separate thread to record the activity.
class ActivityLog : public BrowserContextKeyedService,
                    public TabHelper::ScriptExecutionObserver {
 public:
  // Observers can listen for activity events.
  class Observer {
   public:
    virtual void OnExtensionActivity(scoped_refptr<Action> activity) = 0;
  };

  // ActivityLog is a singleton, so don't instantiate it with the constructor;
  // use GetInstance instead.
  static ActivityLog* GetInstance(Profile* profile);

  // Currently, we only want to record actions if the user has opted in to the
  // ActivityLog feature.
  static bool IsLogEnabled();

  // Recompute whether logging should be enabled (the value of IsLogEnabled is
  // normally cached).  WARNING: This may not be thread-safe, and is only
  // really intended for use by unit tests.
  static void RecomputeLoggingIsEnabled();

  // Add/remove observer.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Log a successful API call made by an extension.
  // This will create an APIAction for storage in the database.
  // (Note: implemented as a wrapper for LogAPIActionInternal.)
  void LogAPIAction(const std::string& extension_id,
                    const std::string& name,    // e.g., tabs.get
                    ListValue* args,            // the argument values e.g. 46
                    const std::string& extra);  // any extra logging info

  // Log an event notification delivered to an extension.
  // This will create an APIAction for storage in the database.
  // (Note: implemented as a wrapper for LogAPIActionInternal.)
  void LogEventAction(const std::string& extension_id,
                      const std::string& name,    // e.g., tabs.onUpdate
                      ListValue* args,            // arguments to the callback
                      const std::string& extra);  // any extra logging info

  // Log a blocked API call made by an extension.
  // This will create a BlockedAction for storage in the database.
  void LogBlockedAction(const std::string& extension_id,
                        const std::string& blocked_call,  // e.g., tabs.get
                        ListValue* args,                  // argument values
                        const BlockedAction::Reason reason,  // why it's blocked
                        const std::string& extra);        // extra logging info

  // Log an interaction between an extension and a URL.
  // This will create a DOMAction for storage in the database.
  void LogDOMAction(const std::string& extension_id,
                    const GURL& url,                      // target URL
                    const string16& url_title,            // title of the URL
                    const std::string& api_call,          // api call
                    const ListValue* args,                // arguments
                    DomActionType::Type call_type,        // type of the call
                    const std::string& extra);            // extra logging info

  // Log a use of the WebRequest API to redirect, cancel, or modify page
  // headers.
  void LogWebRequestAction(const std::string& extension_id,
                           const GURL& url,
                           const std::string& api_call,
                           scoped_ptr<base::DictionaryValue> details,
                           const std::string& extra);

  // Retrieves the list of actions for a given extension on a specific day.
  // Today is 0, yesterday is 1, etc. Returns one day at a time.
  // Response is sent to the method/function in the callback.
  // Use base::Bind to create the callback.
  void GetActions(const std::string& extension_id,
                  const int day,
                  const base::Callback
                      <void(scoped_ptr<std::vector<scoped_refptr<Action> > >)>&
                      callback);

  // For unit tests only.
  void SetArgumentLoggingForTesting(bool log_arguments);

 private:
  friend class ActivityLogFactory;

  explicit ActivityLog(Profile* profile);
  virtual ~ActivityLog();

  // Reset the database in case of persistent catastrophic errors.
  void DatabaseErrorCallback(int error, sql::Statement* stmt);

  // We log callbacks and API calls very similarly, so we handle them the same
  // way internally.
  void LogAPIActionInternal(
      const std::string& extension_id,
      const std::string& api_call,
      ListValue* args,
      const std::string& extra,
      const APIAction::Type type);

  // TabHelper::ScriptExecutionObserver implementation.
  // Fires when a ContentScript is executed.
  virtual void OnScriptsExecuted(
      const content::WebContents* web_contents,
      const ExecutingScriptsMap& extension_ids,
      int32 page_id,
      const GURL& on_url) OVERRIDE;

  // The callback when initializing the database.
  void OnDBInitComplete();

  // The Schedule methods dispatch the calls to the database on a
  // separate thread. We dispatch to the UI thread if the DB thread doesn't
  // exist, which should only happen in tests where there is no DB thread.
  template<typename DatabaseFunc>
  void ScheduleAndForget(DatabaseFunc func) {
    BrowserThread::PostTask(dispatch_thread_,
                            FROM_HERE,
                            base::Bind(func, base::Unretained(db_)));
  }

  template<typename DatabaseFunc, typename ArgA>
  void ScheduleAndForget(DatabaseFunc func, ArgA a) {
    BrowserThread::PostTask(dispatch_thread_,
                            FROM_HERE,
                            base::Bind(func, base::Unretained(db_), a));
  }

  template<typename DatabaseFunc, typename ArgA, typename ArgB>
  void ScheduleAndForget(DatabaseFunc func, ArgA a, ArgB b) {
    BrowserThread::PostTask(dispatch_thread_,
                            FROM_HERE,
                            base::Bind(func, base::Unretained(db_), a, b));
  }

  typedef ObserverListThreadSafe<Observer> ObserverList;

  // The database wrapper that does the actual database I/O.
  // We initialize this on the same thread as the ActivityLog, but then
  // subsequent operations occur on the DB thread. Instead of destructing the
  // ActivityDatabase, we call its Close() method on the DB thread and it
  // commits suicide.
  extensions::ActivityDatabase* db_;

  // Normally the DB thread. In some cases (tests), it might not exist
  // we dispatch to the UI thread.
  BrowserThread::ID dispatch_thread_;

  // Whether to log activity to stdout or the UI. These are set by switches.
  bool log_activity_to_stdout_;
  bool log_activity_to_ui_;

  // testing_mode_ controls whether to log API call arguments. By default, we
  // don't log most arguments to avoid saving too much data. In testing mode,
  // argument collection is enabled. We also whitelist some arguments for
  // collection regardless of whether this bool is true.
  bool testing_mode_;
  base::hash_set<std::string> arg_whitelist_api_;

  Profile* profile_;

  DISALLOW_COPY_AND_ASSIGN(ActivityLog);
};

// Each profile has different extensions, so we keep a different database for
// each profile.
class ActivityLogFactory : public BrowserContextKeyedServiceFactory {
 public:
  static ActivityLog* GetForProfile(Profile* profile) {
    return static_cast<ActivityLog*>(
        GetInstance()->GetServiceForBrowserContext(profile, true));
  }

  static ActivityLogFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<ActivityLogFactory>;
  ActivityLogFactory()
      : BrowserContextKeyedServiceFactory(
          "ActivityLog",
          BrowserContextDependencyManager::GetInstance()) {}
  virtual ~ActivityLogFactory() {}

  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const OVERRIDE;

  virtual content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(ActivityLogFactory);
};


}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_ACTIVITY_LOG_ACTIVITY_LOG_H_
