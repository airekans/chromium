// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/translate/translate_manager.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/memory/singleton.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/prefs/pref_service.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/strings/string_split.h"
#include "base/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/language_state.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/translate/page_translated_details.h"
#include "chrome/browser/translate/translate_infobar_delegate.h"
#include "chrome/browser/translate/translate_language_list.h"
#include "chrome/browser/translate/translate_manager_metrics.h"
#include "chrome/browser/translate/translate_prefs.h"
#include "chrome/browser/translate/translate_tab_helper.h"
#include "chrome/browser/translate/translate_url_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/language_detection_details.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/translate_errors.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "google_apis/google_api_keys.h"
#include "grit/browser_resources.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_request_status.h"
#include "ui/base/resource/resource_bundle.h"

#ifdef FILE_MANAGER_EXTENSION
#include "chrome/browser/chromeos/extensions/file_manager/file_manager_util.h"
#include "extensions/common/constants.h"
#endif

using content::NavigationController;
using content::NavigationEntry;
using content::WebContents;

namespace {

const char kTranslateScriptURL[] =
    "https://translate.google.com/translate_a/element.js";
const char kTranslateScriptHeader[] = "Google-Translate-Element-Mode: library";
const char kReportLanguageDetectionErrorURL[] =
    "https://translate.google.com/translate_error?client=cr&action=langidc";

// Used in kTranslateScriptURL to specify a callback function name.
const char kCallbackQueryName[] = "cb";
const char kCallbackQueryValue[] =
    "cr.googleTranslate.onTranslateElementLoad";

// Used in kReportLanguageDetectionErrorURL to specify the original page
// language.
const char kSourceLanguageQueryName[] = "sl";

// Used in kReportLanguageDetectionErrorURL to specify the page URL.
const char kUrlQueryName[] = "u";

// The delay in ms that we'll wait to check if a page has finished loading
// before attempting a translation.
const int kTranslateLoadCheckDelayMs = 150;

// The maximum number of attempts we'll do to see if the page has finshed
// loading before giving up the translation
const int kMaxTranslateLoadCheckAttempts = 20;

const int kTranslateScriptExpirationDelayDays = 1;

}  // namespace

TranslateManager::~TranslateManager() {
  weak_method_factory_.InvalidateWeakPtrs();
}

// static
TranslateManager* TranslateManager::GetInstance() {
  return Singleton<TranslateManager>::get();
}

// static
bool TranslateManager::IsTranslatableURL(const GURL& url) {
  // A URLs is translatable unless it is one of the following:
  // - empty (can happen for popups created with window.open(""))
  // - an internal URL (chrome:// and others)
  // - the devtools (which is considered UI)
  // - Chrome OS file manager extension
  // - an FTP page (as FTP pages tend to have long lists of filenames that may
  //   confuse the CLD)
  return !url.is_empty() &&
         !url.SchemeIs(chrome::kChromeUIScheme) &&
         !url.SchemeIs(chrome::kChromeDevToolsScheme) &&
#ifdef FILE_MANAGER_EXTENSION
         !(url.SchemeIs(extensions::kExtensionScheme) &&
           url.DomainIs(kFileBrowserDomain)) &&
#endif
         !url.SchemeIs(chrome::kFtpScheme);
}

// static
void TranslateManager::GetSupportedLanguages(
    std::vector<std::string>* languages) {
  if (GetInstance()->language_list_.get()) {
    GetInstance()->language_list_->GetSupportedLanguages(languages);
    return;
  }
  NOTREACHED();
}

// static
std::string TranslateManager::GetLanguageCode(
    const std::string& chrome_locale) {
  if (GetInstance()->language_list_.get())
    return GetInstance()->language_list_->GetLanguageCode(chrome_locale);
  NOTREACHED();
  return chrome_locale;
}

// static
bool TranslateManager::IsSupportedLanguage(const std::string& language) {
  if (GetInstance()->language_list_.get())
    return GetInstance()->language_list_->IsSupportedLanguage(language);
  NOTREACHED();
  return false;
}

void TranslateManager::Observe(int type,
                               const content::NotificationSource& source,
                               const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_NAV_ENTRY_COMMITTED: {
      NavigationController* controller =
          content::Source<NavigationController>(source).ptr();
      content::LoadCommittedDetails* load_details =
          content::Details<content::LoadCommittedDetails>(details).ptr();
      NavigationEntry* entry = controller->GetActiveEntry();
      if (!entry) {
        NOTREACHED();
        return;
      }

      TranslateTabHelper* translate_tab_helper =
          TranslateTabHelper::FromWebContents(controller->GetWebContents());
      if (!translate_tab_helper)
        return;

      // If the navigation happened while offline don't show the translate
      // bar since there will be nothing to translate.
      if (load_details->http_status_code == 0 ||
          load_details->http_status_code == net::HTTP_INTERNAL_SERVER_ERROR) {
        return;
      }

      if (!load_details->is_main_frame &&
          translate_tab_helper->language_state().translation_declined()) {
        // Some sites (such as Google map) may trigger sub-frame navigations
        // when the user interacts with the page.  We don't want to show a new
        // infobar if the user already dismissed one in that case.
        return;
      }
      if (entry->GetTransitionType() != content::PAGE_TRANSITION_RELOAD &&
          load_details->type != content::NAVIGATION_TYPE_SAME_PAGE) {
        return;
      }

      // When doing a page reload, TAB_LANGUAGE_DETERMINED is not sent,
      // so the translation needs to be explicitly initiated, but only when the
      // page needs translation.
      if (!translate_tab_helper->language_state().page_needs_translation())
        return;
      // Note that we delay it as the TranslateManager gets this notification
      // before the WebContents and the WebContents processing might remove the
      // current infobars.  Since InitTranslation might add an infobar, it must
      // be done after that.
      MessageLoop::current()->PostTask(FROM_HERE,
          base::Bind(
              &TranslateManager::InitiateTranslationPosted,
              weak_method_factory_.GetWeakPtr(),
              controller->GetWebContents()->GetRenderProcessHost()->GetID(),
              controller->GetWebContents()->GetRenderViewHost()->GetRoutingID(),
              translate_tab_helper->language_state().original_language(), 0));
      break;
    }
    case chrome::NOTIFICATION_TAB_LANGUAGE_DETERMINED: {
      const LanguageDetectionDetails* lang_det_details =
          content::Details<const LanguageDetectionDetails>(details).ptr();
      NotifyLanguageDetection(*lang_det_details);

      WebContents* tab = content::Source<WebContents>(source).ptr();
      // We may get this notifications multiple times.  Make sure to translate
      // only once.
      TranslateTabHelper* translate_tab_helper =
          TranslateTabHelper::FromWebContents(tab);
      if (!translate_tab_helper)
        return;

      LanguageState& language_state = translate_tab_helper->language_state();
      if (language_state.page_needs_translation() &&
          !language_state.translation_pending() &&
          !language_state.translation_declined() &&
          !language_state.IsPageTranslated()) {
        std::string language = lang_det_details->adopted_language;
        InitiateTranslation(tab, language);
      }
      break;
    }
    case chrome::NOTIFICATION_PAGE_TRANSLATED: {
      // Only add translate infobar if it doesn't exist; if it already exists,
      // just update the state, the actual infobar would have received the same
      //  notification and update the visual display accordingly.
      WebContents* tab = content::Source<WebContents>(source).ptr();
      PageTranslatedDetails* page_translated_details =
          content::Details<PageTranslatedDetails>(details).ptr();
      PageTranslated(tab, page_translated_details);
      break;
    }
    case chrome::NOTIFICATION_PROFILE_DESTROYED: {
      PrefService* pref_service =
          content::Source<Profile>(source).ptr()->GetPrefs();
      notification_registrar_.Remove(this,
                                     chrome::NOTIFICATION_PROFILE_DESTROYED,
                                     source);
      size_t count = accept_languages_.erase(pref_service);
      // We should know about this profile since we are listening for
      // notifications on it.
      DCHECK(count == 1u);
      PrefChangeRegistrar* pref_change_registrar =
          pref_change_registrars_[pref_service];
      count = pref_change_registrars_.erase(pref_service);
      DCHECK(count == 1u);
      delete pref_change_registrar;
      break;
    }
    default:
      NOTREACHED();
  }
}

void TranslateManager::OnURLFetchComplete(const net::URLFetcher* source) {
  if (translate_script_request_pending_.get() != source) {
    // Looks like crash on Mac is possibly caused with callback entering here
    // with unknown fetcher when network is refreshed.
    scoped_ptr<const net::URLFetcher> delete_ptr(source);
    return;
  }

  bool error =
      source->GetStatus().status() != net::URLRequestStatus::SUCCESS ||
      source->GetResponseCode() != net::HTTP_OK;
  if (translate_script_request_pending_.get() == source) {
    scoped_ptr<const net::URLFetcher> delete_ptr(
        translate_script_request_pending_.release());
    if (!error) {
      base::StringPiece str = ResourceBundle::GetSharedInstance().
          GetRawDataResource(IDR_TRANSLATE_JS);
      DCHECK(translate_script_.empty());
      str.CopyToString(&translate_script_);
      std::string argument = "('";
      std::string api_key = google_apis::GetAPIKey();
      argument += net::EscapeQueryParamValue(api_key, true);
      argument += "');\n";
      std::string data;
      source->GetResponseAsString(&data);
      translate_script_ += argument + data;

      // We'll expire the cached script after some time, to make sure long
      // running browsers still get fixes that might get pushed with newer
      // scripts.
      MessageLoop::current()->PostDelayedTask(FROM_HERE,
          base::Bind(&TranslateManager::ClearTranslateScript,
                     weak_method_factory_.GetWeakPtr()),
          translate_script_expiration_delay_);
    }
    // Process any pending requests.
    std::vector<PendingRequest>::const_iterator iter;
    for (iter = pending_requests_.begin(); iter != pending_requests_.end();
         ++iter) {
      const PendingRequest& request = *iter;
      WebContents* web_contents =
          tab_util::GetWebContentsByID(request.render_process_id,
                                       request.render_view_id);
      if (!web_contents) {
        // The tab went away while we were retrieving the script.
        continue;
      }
      NavigationEntry* entry = web_contents->GetController().GetActiveEntry();
      if (!entry || entry->GetPageID() != request.page_id) {
        // We navigated away from the page the translation was triggered on.
        continue;
      }

      if (error) {
        Profile* profile =
            Profile::FromBrowserContext(web_contents->GetBrowserContext());
        TranslateInfoBarDelegate::Create(
            InfoBarService::FromWebContents(web_contents),
            true,
            TranslateInfoBarDelegate::TRANSLATION_ERROR,
            TranslateErrors::NETWORK,
            profile->GetPrefs(),
            ShortcutConfig(),
            request.source_lang,
            request.target_lang);
      } else {
        // Translate the page.
        DoTranslatePage(web_contents, translate_script_,
                        request.source_lang, request.target_lang);
      }
    }
    pending_requests_.clear();
  }
}

void TranslateManager::AddObserver(Observer* obs) {
  observer_list_.AddObserver(obs);
}

void TranslateManager::RemoveObserver(Observer* obs) {
  observer_list_.RemoveObserver(obs);
}

void TranslateManager::NotifyLanguageDetection(
    const LanguageDetectionDetails& details) {
  FOR_EACH_OBSERVER(Observer, observer_list_, OnLanguageDetection(details));
}


TranslateManager::TranslateManager()
  : weak_method_factory_(this),
    translate_script_expiration_delay_(base::TimeDelta::FromDays(
        kTranslateScriptExpirationDelayDays)),
    max_reload_check_attempts_(kMaxTranslateLoadCheckAttempts) {
  notification_registrar_.Add(this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
                              content::NotificationService::AllSources());
  notification_registrar_.Add(this,
                              chrome::NOTIFICATION_TAB_LANGUAGE_DETERMINED,
                              content::NotificationService::AllSources());
  notification_registrar_.Add(this, chrome::NOTIFICATION_PAGE_TRANSLATED,
                              content::NotificationService::AllSources());
  language_list_.reset(new TranslateLanguageList);
}

void TranslateManager::InitiateTranslation(WebContents* web_contents,
                                           const std::string& page_lang) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  PrefService* prefs = profile->GetOriginalProfile()->GetPrefs();
  if (!prefs->GetBoolean(prefs::kEnableTranslate)) {
    TranslateManagerMetrics::ReportInitiationStatus(
        TranslateManagerMetrics::INITIATION_STATUS_DISABLED_BY_PREFS);
    return;
  }

  // Allow disabling of translate from the command line to assist with
  // automated browser testing.
  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableTranslate)) {
    TranslateManagerMetrics::ReportInitiationStatus(
        TranslateManagerMetrics::INITIATION_STATUS_DISABLED_BY_SWITCH);
    return;
  }

  // Don't translate any Chrome specific page, e.g., New Tab Page, Download,
  // History, and so on.
  GURL page_url = web_contents->GetURL();
  if (!IsTranslatableURL(page_url)) {
    TranslateManagerMetrics::ReportInitiationStatus(
        TranslateManagerMetrics::INITIATION_STATUS_URL_IS_NOT_SUPPORTED);
    return;
  }

  // Don't translate similar languages (ex: en-US to en).
  std::string target_lang = GetTargetLanguage(prefs);
  std::string language_code = GetLanguageCode(page_lang);
  if (language_code == target_lang) {
    TranslateManagerMetrics::ReportInitiationStatus(
        TranslateManagerMetrics::INITIATION_STATUS_SIMILAR_LANGUAGES);
    return;
  }

  // Don't translate any language the user configured as accepted languages.
  if (IsAcceptLanguage(web_contents, language_code)) {
    TranslateManagerMetrics::ReportInitiationStatus(
        TranslateManagerMetrics::INITIATION_STATUS_ACCEPT_LANGUAGES);
    return;
  }

  // Nothing to do if either the language Chrome is in or the language of the
  // page is not supported by the translation server.
  if (target_lang.empty() || !IsSupportedLanguage(language_code)) {
    TranslateManagerMetrics::ReportInitiationStatus(
        TranslateManagerMetrics::INITIATION_STATUS_LANGUAGE_IS_NOT_SUPPORTED);
    return;
  }

  // Don't translate any user black-listed URLs or user selected language
  // combination.
  if (!TranslatePrefs::CanTranslate(prefs, language_code, page_url)) {
    TranslateManagerMetrics::ReportInitiationStatus(
        TranslateManagerMetrics::INITIATION_STATUS_DISABLED_BY_CONFIG);
    return;
  }

  // If the user has previously selected "always translate" for this language we
  // automatically translate.  Note that in incognito mode we disable that
  // feature; the user will get an infobar, so they can control whether the
  // page's text is sent to the translate server.
  std::string auto_target_lang;
  if (!web_contents->GetBrowserContext()->IsOffTheRecord() &&
      TranslatePrefs::ShouldAutoTranslate(prefs, language_code,
          &auto_target_lang)) {
    // We need to confirm that the saved target language is still supported.
    // Also, GetLanguageCode will take care of removing country code if any.
    auto_target_lang = GetLanguageCode(auto_target_lang);
    if (IsSupportedLanguage(auto_target_lang)) {
      TranslateManagerMetrics::ReportInitiationStatus(
          TranslateManagerMetrics::INITIATION_STATUS_AUTO_BY_CONFIG);
      TranslatePage(web_contents, language_code, auto_target_lang);
      return;
    }
  }

  TranslateTabHelper* translate_tab_helper =
      TranslateTabHelper::FromWebContents(web_contents);
  if (!translate_tab_helper)
    return;

  std::string auto_translate_to =
      translate_tab_helper->language_state().AutoTranslateTo();
  if (!auto_translate_to.empty()) {
    // This page was navigated through a click from a translated page.
    TranslateManagerMetrics::ReportInitiationStatus(
        TranslateManagerMetrics::INITIATION_STATUS_AUTO_BY_LINK);
    TranslatePage(web_contents, language_code, auto_translate_to);
    return;
  }

  // Prompts the user if he/she wants the page translated.
  TranslateManagerMetrics::ReportInitiationStatus(
      TranslateManagerMetrics::INITIATION_STATUS_SHOW_INFOBAR);
  TranslateInfoBarDelegate::Create(
      InfoBarService::FromWebContents(web_contents), false,
      TranslateInfoBarDelegate::BEFORE_TRANSLATE, TranslateErrors::NONE,
      profile->GetPrefs(), ShortcutConfig(),
      language_code, target_lang);
}

void TranslateManager::InitiateTranslationPosted(
    int process_id, int render_id, const std::string& page_lang, int attempt) {
  // The tab might have been closed.
  WebContents* web_contents =
      tab_util::GetWebContentsByID(process_id, render_id);
  if (!web_contents)
    return;

  TranslateTabHelper* translate_tab_helper =
      TranslateTabHelper::FromWebContents(web_contents);
  if (translate_tab_helper->language_state().translation_pending())
    return;

  // During a reload we need web content to be available before the
  // translate script is executed. Otherwise we will run the translate script on
  // an empty DOM which will fail. Therefore we wait a bit to see if the page
  // has finished.
  if ((web_contents->IsLoading()) && attempt < kMaxTranslateLoadCheckAttempts) {
    int backoff = attempt * max_reload_check_attempts_;
    MessageLoop::current()->PostDelayedTask(
        FROM_HERE, base::Bind(&TranslateManager::InitiateTranslationPosted,
                              weak_method_factory_.GetWeakPtr(), process_id,
                              render_id, page_lang, ++attempt),
        base::TimeDelta::FromMilliseconds(backoff));
    return;
  }

  InitiateTranslation(web_contents, GetLanguageCode(page_lang));
}

void TranslateManager::TranslatePage(WebContents* web_contents,
                                     const std::string& original_source_lang,
                                     const std::string& target_lang) {
  NavigationEntry* entry = web_contents->GetController().GetActiveEntry();
  if (!entry) {
    NOTREACHED();
    return;
  }

  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());

  std::string source_lang(original_source_lang);

  // Translation can be kicked by context menu against unsupported languages.
  // Unsupported language strings should be replaced with
  // kUnknownLanguageCode in order to send a translation request with enabling
  // server side auto language detection.
  if (!IsSupportedLanguage(source_lang))
    source_lang = std::string(chrome::kUnknownLanguageCode);

  TranslateInfoBarDelegate::Create(
      InfoBarService::FromWebContents(web_contents), true,
      TranslateInfoBarDelegate::TRANSLATING, TranslateErrors::NONE,
      profile->GetPrefs(), ShortcutConfig(), source_lang, target_lang);

  if (!translate_script_.empty()) {
    DoTranslatePage(web_contents, translate_script_, source_lang, target_lang);
    return;
  }

  // The script is not available yet.  Queue that request and query for the
  // script.  Once it is downloaded we'll do the translate.
  content::RenderViewHost* rvh = web_contents->GetRenderViewHost();
  PendingRequest request;
  request.render_process_id = rvh->GetProcess()->GetID();
  request.render_view_id = rvh->GetRoutingID();
  request.page_id = entry->GetPageID();
  request.source_lang = source_lang;
  request.target_lang = target_lang;
  pending_requests_.push_back(request);
  RequestTranslateScript();
}

void TranslateManager::RevertTranslation(WebContents* web_contents) {
  NavigationEntry* entry = web_contents->GetController().GetActiveEntry();
  if (!entry) {
    NOTREACHED();
    return;
  }
  web_contents->GetRenderViewHost()->Send(new ChromeViewMsg_RevertTranslation(
      web_contents->GetRenderViewHost()->GetRoutingID(), entry->GetPageID()));

  TranslateTabHelper* translate_tab_helper =
      TranslateTabHelper::FromWebContents(web_contents);
  translate_tab_helper->language_state().set_current_language(
      translate_tab_helper->language_state().original_language());
}

void TranslateManager::ReportLanguageDetectionError(WebContents* web_contents) {
  TranslateManagerMetrics::ReportLanguageDetectionError();
  // We'll open the URL in a new tab so that the user can tell us more.
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents);
  if (!browser) {
    NOTREACHED();
    return;
  }

  GURL report_error_url = GURL(kReportLanguageDetectionErrorURL);

  GURL page_url = web_contents->GetController().GetActiveEntry()->GetURL();
  report_error_url = net::AppendQueryParameter(
      report_error_url,
      kUrlQueryName,
      page_url.spec());

  TranslateTabHelper* translate_tab_helper =
      TranslateTabHelper::FromWebContents(web_contents);
  report_error_url = net::AppendQueryParameter(
      report_error_url,
      kSourceLanguageQueryName,
      translate_tab_helper->language_state().original_language());

  report_error_url = TranslateURLUtil::AddHostLocaleToUrl(report_error_url);
  report_error_url = TranslateURLUtil::AddApiKeyToUrl(report_error_url);

  chrome::AddSelectedTabWithURL(browser, report_error_url,
                                content::PAGE_TRANSITION_AUTO_BOOKMARK);
}

void TranslateManager::DoTranslatePage(WebContents* web_contents,
                                       const std::string& translate_script,
                                       const std::string& source_lang,
                                       const std::string& target_lang) {
  NavigationEntry* entry = web_contents->GetController().GetActiveEntry();
  if (!entry) {
    NOTREACHED();
    return;
  }

  TranslateTabHelper* translate_tab_helper =
      TranslateTabHelper::FromWebContents(web_contents);
  if (!translate_tab_helper)
    return;

  translate_tab_helper->language_state().set_translation_pending(true);
  web_contents->GetRenderViewHost()->Send(new ChromeViewMsg_TranslatePage(
      web_contents->GetRenderViewHost()->GetRoutingID(), entry->GetPageID(),
      translate_script, source_lang, target_lang));
}

void TranslateManager::PageTranslated(WebContents* web_contents,
                                      PageTranslatedDetails* details) {
  if ((details->error_type == TranslateErrors::NONE) &&
      details->source_language != chrome::kUnknownLanguageCode &&
      !IsSupportedLanguage(details->source_language)) {
    // TODO(toyoshim): http://crbug.com/242142 We should check if
    // l10n_util::GetDisplayNameForLocale() support |source_language| here.
    // Also, following metrics should be modified to have language code.
    TranslateManagerMetrics::ReportUnsupportedLanguage();
    details->error_type = TranslateErrors::UNSUPPORTED_LANGUAGE;
  }

  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  PrefService* prefs = profile->GetPrefs();
  TranslateInfoBarDelegate::Create(
      InfoBarService::FromWebContents(web_contents), true,
      (details->error_type == TranslateErrors::NONE) ?
          TranslateInfoBarDelegate::AFTER_TRANSLATE :
          TranslateInfoBarDelegate::TRANSLATION_ERROR,
      details->error_type, prefs, ShortcutConfig(), details->source_language,
      details->target_language);
}

bool TranslateManager::IsAcceptLanguage(WebContents* web_contents,
                                        const std::string& language) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  profile = profile->GetOriginalProfile();
  PrefService* pref_service = profile->GetPrefs();
  PrefServiceLanguagesMap::const_iterator iter =
      accept_languages_.find(pref_service);
  if (iter == accept_languages_.end()) {
    InitAcceptLanguages(pref_service);
    // Listen for this profile going away, in which case we would need to clear
    // the accepted languages for the profile.
    notification_registrar_.Add(this, chrome::NOTIFICATION_PROFILE_DESTROYED,
                                content::Source<Profile>(profile));
    // Also start listening for changes in the accept languages.
    DCHECK(pref_change_registrars_.find(pref_service) ==
           pref_change_registrars_.end());
    PrefChangeRegistrar* pref_change_registrar = new PrefChangeRegistrar;
    pref_change_registrar->Init(pref_service);
    pref_change_registrar->Add(
        prefs::kAcceptLanguages,
        base::Bind(&TranslateManager::InitAcceptLanguages,
                   base::Unretained(this),
                   pref_service));
    pref_change_registrars_[pref_service] = pref_change_registrar;

    iter = accept_languages_.find(pref_service);
  }

  return iter->second.count(language) != 0;
}

void TranslateManager::InitAcceptLanguages(PrefService* prefs) {
  // We have been asked for this profile, build the languages.
  std::string accept_langs_str = prefs->GetString(prefs::kAcceptLanguages);
  std::vector<std::string> accept_langs_list;
  LanguageSet accept_langs_set;
  base::SplitString(accept_langs_str, ',', &accept_langs_list);
  std::vector<std::string>::const_iterator iter;
  std::string ui_lang =
      GetLanguageCode(g_browser_process->GetApplicationLocale());
  bool is_ui_english = StartsWithASCII(ui_lang, "en-", false);
  for (iter = accept_langs_list.begin();
       iter != accept_langs_list.end(); ++iter) {
    // Get rid of the locale extension if any (ex: en-US -> en), but for Chinese
    // for which the CLD reports zh-CN and zh-TW.
    std::string accept_lang(*iter);
    size_t index = iter->find("-");
    if (index != std::string::npos && *iter != "zh-CN" && *iter != "zh-TW")
      accept_lang = iter->substr(0, index);
    // Special-case English until we resolve bug 36182 properly.
    // Add English only if the UI language is not English. This will annoy
    // users of non-English Chrome who can comprehend English until English is
    // black-listed.
    // TODO(jungshik): Once we determine that it's safe to remove English from
    // the default Accept-Language values for most locales, remove this
    // special-casing.
    if (accept_lang != "en" || is_ui_english)
      accept_langs_set.insert(accept_lang);
  }
  accept_languages_[prefs] = accept_langs_set;
}

void TranslateManager::FetchLanguageListFromTranslateServer(
    PrefService* prefs) {
  DCHECK(language_list_.get());

  // We don't want to do this when translate is disabled.
  DCHECK(prefs != NULL);
  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableTranslate) ||
      (prefs != NULL && !prefs->GetBoolean(prefs::kEnableTranslate))) {
    return;
  }

  if (language_list_.get())
    language_list_->RequestLanguageList();
}

void TranslateManager::CleanupPendingUlrFetcher() {
  language_list_.reset();
  translate_script_request_pending_.reset();
}

void TranslateManager::RequestTranslateScript() {
  if (translate_script_request_pending_.get() != NULL)
    return;

  GURL translate_script_url;
  // Check if command-line contains an alternative URL for translate service.
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kTranslateScriptURL)) {
    translate_script_url = GURL(
        command_line.GetSwitchValueASCII(switches::kTranslateScriptURL));
    if (!translate_script_url.is_valid() ||
        !translate_script_url.query().empty()) {
      LOG(WARNING) << "The following translate URL specified at the "
                   << "command-line is invalid: "
                   << translate_script_url.spec();
      translate_script_url = GURL();
    }
  }
  // Use default URL when command-line argument is not specified, or specified
  // URL is invalid.
  if (translate_script_url.is_empty())
    translate_script_url = GURL(kTranslateScriptURL);

  translate_script_url = net::AppendQueryParameter(
      translate_script_url,
      kCallbackQueryName,
      kCallbackQueryValue);

  translate_script_url =
      TranslateURLUtil::AddHostLocaleToUrl(translate_script_url);
  translate_script_url =
      TranslateURLUtil::AddApiKeyToUrl(translate_script_url);

  translate_script_request_pending_.reset(net::URLFetcher::Create(
      0, translate_script_url, net::URLFetcher::GET, this));
  translate_script_request_pending_->SetLoadFlags(
      net::LOAD_DO_NOT_SEND_COOKIES | net::LOAD_DO_NOT_SAVE_COOKIES);
  translate_script_request_pending_->SetRequestContext(
      g_browser_process->system_request_context());
  translate_script_request_pending_->SetExtraRequestHeaders(
      kTranslateScriptHeader);
  translate_script_request_pending_->Start();
}

// static
std::string TranslateManager::GetTargetLanguage(PrefService* prefs) {
  std::string ui_lang =
      GetLanguageCode(g_browser_process->GetApplicationLocale());
  if (IsSupportedLanguage(ui_lang))
    return ui_lang;

  // Getting the accepted languages list
  std::string accept_langs_str = prefs->GetString(prefs::kAcceptLanguages);

  std::vector<std::string> accept_langs_list;
  base::SplitString(accept_langs_str, ',', &accept_langs_list);

  // Will translate to the first supported language on the Accepted Language
  // list or not at all if no such candidate exists
  std::vector<std::string>::iterator iter;
  for (iter = accept_langs_list.begin();
       iter != accept_langs_list.end(); ++iter) {
    std::string lang_code = GetLanguageCode(*iter);
    if (IsSupportedLanguage(lang_code))
      return lang_code;
  }
  return std::string();
}

// static
ShortcutConfiguration TranslateManager::ShortcutConfig() {
  ShortcutConfiguration config;

  // The android implementation does not offer a drop down for space
  // reason so we are more aggressive showing the shortcuts for never translate.
  #if defined(OS_ANDROID)
  config.never_translate_min_count = 1;
  #else
  config.never_translate_min_count = 3;
  #endif  // defined(OS_ANDROID)

  config.always_translate_min_count = 3;
  return config;
}
