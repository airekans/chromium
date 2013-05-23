// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/autofill/autofill_dialog_controller_impl.h"

#include <algorithm>
#include <string>

#include "base/bind.h"
#include "base/logging.h"
#include "base/prefs/pref_service.h"
#include "base/string_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/shell_window_registry.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view.h"
#include "chrome/browser/ui/autofill/data_model_wrapper.h"
#include "chrome/browser/ui/base_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/extensions/native_app_window.h"
#include "chrome/browser/ui/extensions/shell_window.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "components/autofill/browser/autofill_country.h"
#include "components/autofill/browser/autofill_data_model.h"
#include "components/autofill/browser/autofill_manager.h"
#include "components/autofill/browser/autofill_type.h"
#include "components/autofill/browser/personal_data_manager.h"
#include "components/autofill/browser/phone_number_i18n.h"
#include "components/autofill/browser/risk/fingerprint.h"
#include "components/autofill/browser/risk/proto/fingerprint.pb.h"
#include "components/autofill/browser/validation.h"
#include "components/autofill/browser/wallet/cart.h"
#include "components/autofill/browser/wallet/full_wallet.h"
#include "components/autofill/browser/wallet/instrument.h"
#include "components/autofill/browser/wallet/wallet_address.h"
#include "components/autofill/browser/wallet/wallet_items.h"
#include "components/autofill/browser/wallet/wallet_service_url.h"
#include "components/autofill/browser/wallet/wallet_signin_helper.h"
#include "components/autofill/common/form_data.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/geolocation_provider.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/common/url_constants.h"
#include "grit/chromium_strings.h"
#include "grit/component_resources.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/webkit_resources.h"
#include "net/cert/cert_status_flags.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/skbitmap_operations.h"

namespace autofill {

namespace {

const bool kPayWithoutWalletDefault = false;

// This is a pseudo-scientifically chosen maximum amount we want a fronting
// (proxy) card to be able to charge. The current actual max is $2000. Using
// only $1850 leaves some room for tax and shipping, etc. TODO(dbeam): send a
// special value to the server to just ask for the maximum so we don't need to
// hardcode it here (http://crbug.com/180731). TODO(dbeam): also maybe allow
// users to give us this number via an <input> (http://crbug.com/180733).
const int kCartMax = 1850;
const char kCartCurrency[] = "USD";

const char kAddNewItemKey[] = "add-new-item";
const char kManageItemsKey[] = "manage-items";
const char kSameAsBillingKey[] = "same-as-billing";

// This string is stored along with saved addresses and credit cards in the
// WebDB, and hence should not be modified, so that it remains consistent over
// time.
const char kAutofillDialogOrigin[] = "Chrome Autofill dialog";

// HSL shift to gray out an image.
const color_utils::HSL kGrayImageShift = {-1, 0, 0.8};

// Returns true if |input| should be shown when |field_type| has been requested.
bool InputTypeMatchesFieldType(const DetailInput& input,
                               AutofillFieldType field_type) {
  // If any credit card expiration info is asked for, show both month and year
  // inputs.
  if (field_type == CREDIT_CARD_EXP_4_DIGIT_YEAR ||
      field_type == CREDIT_CARD_EXP_2_DIGIT_YEAR ||
      field_type == CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR ||
      field_type == CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR ||
      field_type == CREDIT_CARD_EXP_MONTH) {
    return input.type == CREDIT_CARD_EXP_4_DIGIT_YEAR ||
           input.type == CREDIT_CARD_EXP_MONTH;
  }

  if (field_type == CREDIT_CARD_TYPE)
    return input.type == CREDIT_CARD_NUMBER;

  return input.type == field_type;
}

// Returns true if |input| should be used for a site-requested |field|.
bool DetailInputMatchesField(const DetailInput& input,
                             const AutofillField& field) {
  return InputTypeMatchesFieldType(input, field.type());
}

bool IsCreditCardType(AutofillFieldType type) {
  return AutofillType(type).group() == AutofillType::CREDIT_CARD;
}

// Returns true if |input| should be used to fill a site-requested |field| which
// is notated with a "shipping" tag, for use when the user has decided to use
// the billing address as the shipping address.
bool DetailInputMatchesShippingField(const DetailInput& input,
                                     const AutofillField& field) {
  if (field.type() == NAME_FULL)
    return input.type == CREDIT_CARD_NAME;

  // Equivalent billing field type is used to support UseBillingAsShipping
  // usecase.
  AutofillFieldType field_type =
      AutofillType::GetEquivalentBillingFieldType(field.type());

  return InputTypeMatchesFieldType(input, field_type);
}

// Constructs |inputs| from template data.
void BuildInputs(const DetailInput* input_template,
                 size_t template_size,
                 DetailInputs* inputs) {
  for (size_t i = 0; i < template_size; ++i) {
    const DetailInput* input = &input_template[i];
    inputs->push_back(*input);
  }
}

// Initializes |form_group| from user-entered data.
void FillFormGroupFromOutputs(const DetailOutputMap& detail_outputs,
                              FormGroup* form_group) {
  for (DetailOutputMap::const_iterator iter = detail_outputs.begin();
       iter != detail_outputs.end(); ++iter) {
    if (!iter->second.empty()) {
      AutofillFieldType type = iter->first->type;
      if (type == ADDRESS_HOME_COUNTRY || type == ADDRESS_BILLING_COUNTRY) {
        form_group->SetInfo(type,
                            iter->second,
                            g_browser_process->GetApplicationLocale());
      } else {
        form_group->SetRawInfo(iter->first->type, iter->second);
      }
    }
  }
}

// Get billing info from |output| and put it into |card|, |cvc|, and |profile|.
// These outparams are required because |card|/|profile| accept different types
// of raw info, and CreditCard doesn't save CVCs.
void GetBillingInfoFromOutputs(const DetailOutputMap& output,
                               CreditCard* card,
                               string16* cvc,
                               AutofillProfile* profile) {
  for (DetailOutputMap::const_iterator it = output.begin();
       it != output.end(); ++it) {
    string16 trimmed;
    TrimWhitespace(it->second, TRIM_ALL, &trimmed);

    // Special case CVC as CreditCard just swallows it.
    if (it->first->type == CREDIT_CARD_VERIFICATION_CODE) {
      if (cvc)
        cvc->assign(trimmed);
    } else if (it->first->type == ADDRESS_HOME_COUNTRY ||
               it->first->type == ADDRESS_BILLING_COUNTRY) {
        profile->SetInfo(it->first->type,
                         trimmed,
                         g_browser_process->GetApplicationLocale());
    } else {
      // Copy the credit card name to |profile| in addition to |card| as
      // wallet::Instrument requires a recipient name for its billing address.
      if (profile && it->first->type == CREDIT_CARD_NAME)
        profile->SetRawInfo(NAME_FULL, trimmed);

      if (IsCreditCardType(it->first->type)) {
        if (card)
          card->SetRawInfo(it->first->type, trimmed);
      } else if (profile) {
        profile->SetRawInfo(it->first->type, trimmed);
      }
    }
  }
}

// Returns the containing window for the given |web_contents|. The containing
// window might be a browser window for a Chrome tab, or it might be a shell
// window for a platform app.
BaseWindow* GetBaseWindowForWebContents(
    const content::WebContents* web_contents) {
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents);
  if (browser)
    return browser->window();

  gfx::NativeWindow native_window =
      web_contents->GetView()->GetTopLevelNativeWindow();
  ShellWindow* shell_window =
      extensions::ShellWindowRegistry::
          GetShellWindowForNativeWindowAnyProfile(native_window);
  return shell_window->GetBaseWindow();
}

// Extracts the string value of a field with |type| from |output|. This is
// useful when you only need the value of 1 input from a section of view inputs.
string16 GetValueForType(const DetailOutputMap& output,
                         AutofillFieldType type) {
  for (DetailOutputMap::const_iterator it = output.begin();
       it != output.end(); ++it) {
    if (it->first->type == type)
      return it->second;
  }
  NOTREACHED();
  return string16();
}

// Check if a given MaskedInstrument is allowed for the purchase.
bool IsInstrumentAllowed(
    const wallet::WalletItems::MaskedInstrument& instrument) {
  return (instrument.status() == wallet::WalletItems::MaskedInstrument::VALID ||
      instrument.status() == wallet::WalletItems::MaskedInstrument::PENDING) &&
      instrument.type() != wallet::WalletItems::MaskedInstrument::AMEX &&
      instrument.type() != wallet::WalletItems::MaskedInstrument::UNKNOWN;
}

// Signals that the user has opted in to geolocation services.  Factored out
// into a separate method because all interaction with the geolocation provider
// needs to happen on the IO thread, which is not the thread
// AutofillDialogController lives on.
void UserDidOptIntoLocationServices() {
  content::GeolocationProvider::GetInstance()->UserDidOptIntoLocationServices();
}

}  // namespace

AutofillDialogController::~AutofillDialogController() {}

AutofillDialogControllerImpl::~AutofillDialogControllerImpl() {
  if (popup_controller_)
    popup_controller_->Hide();

  GetMetricLogger().LogDialogInitialUserState(
      GetDialogType(), initial_user_state_);
}

// static
base::WeakPtr<AutofillDialogControllerImpl>
    AutofillDialogControllerImpl::Create(
    content::WebContents* contents,
    const FormData& form_structure,
    const GURL& source_url,
    const DialogType dialog_type,
    const base::Callback<void(const FormStructure*,
                              const std::string&)>& callback) {
  // AutofillDialogControllerImpl owns itself.
  AutofillDialogControllerImpl* autofill_dialog_controller =
      new AutofillDialogControllerImpl(contents,
                                       form_structure,
                                       source_url,
                                       dialog_type,
                                       callback);
  return autofill_dialog_controller->weak_ptr_factory_.GetWeakPtr();
}

// static
void AutofillDialogControllerImpl::RegisterUserPrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      ::prefs::kAutofillDialogPayWithoutWallet,
      kPayWithoutWalletDefault,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
}

void AutofillDialogControllerImpl::Show() {
  dialog_shown_timestamp_ = base::Time::Now();

  content::NavigationEntry* entry = contents_->GetController().GetActiveEntry();
  const GURL& active_url = entry ? entry->GetURL() : contents_->GetURL();
  invoked_from_same_origin_ = active_url.GetOrigin() == source_url_.GetOrigin();

  // Log any relevant UI metrics and security exceptions.
  GetMetricLogger().LogDialogUiEvent(
      GetDialogType(), AutofillMetrics::DIALOG_UI_SHOWN);

  GetMetricLogger().LogDialogSecurityMetric(
      GetDialogType(), AutofillMetrics::SECURITY_METRIC_DIALOG_SHOWN);

  if (RequestingCreditCardInfo() && !TransmissionWillBeSecure()) {
    GetMetricLogger().LogDialogSecurityMetric(
        GetDialogType(),
        AutofillMetrics::SECURITY_METRIC_CREDIT_CARD_OVER_HTTP);
  }

  if (!invoked_from_same_origin_) {
    GetMetricLogger().LogDialogSecurityMetric(
        GetDialogType(),
        AutofillMetrics::SECURITY_METRIC_CROSS_ORIGIN_FRAME);
  }

  // Determine what field types should be included in the dialog.
  bool has_types = false;
  bool has_sections = false;
  form_structure_.ParseFieldTypesFromAutocompleteAttributes(&has_types,
                                                            &has_sections);
  // Fail if the author didn't specify autocomplete types.
  if (!has_types) {
    callback_.Run(NULL, std::string());
    delete this;
    return;
  }

  const DetailInput kEmailInputs[] = {
    { 1, EMAIL_ADDRESS, IDS_AUTOFILL_DIALOG_PLACEHOLDER_EMAIL },
  };

  const DetailInput kCCInputs[] = {
    { 2, CREDIT_CARD_NUMBER, IDS_AUTOFILL_DIALOG_PLACEHOLDER_CARD_NUMBER },
    { 3, CREDIT_CARD_EXP_MONTH },
    { 3, CREDIT_CARD_EXP_4_DIGIT_YEAR },
    { 3, CREDIT_CARD_VERIFICATION_CODE, IDS_AUTOFILL_DIALOG_PLACEHOLDER_CVC },
    { 4, CREDIT_CARD_NAME, IDS_AUTOFILL_DIALOG_PLACEHOLDER_CARDHOLDER_NAME },
  };

  const DetailInput kBillingInputs[] = {
    { 5, ADDRESS_BILLING_LINE1,
      IDS_AUTOFILL_DIALOG_PLACEHOLDER_ADDRESS_LINE_1 },
    { 6, ADDRESS_BILLING_LINE2,
      IDS_AUTOFILL_DIALOG_PLACEHOLDER_ADDRESS_LINE_2 },
    { 7, ADDRESS_BILLING_CITY,
      IDS_AUTOFILL_DIALOG_PLACEHOLDER_LOCALITY },
    // TODO(estade): state placeholder should depend on locale.
    { 8, ADDRESS_BILLING_STATE, IDS_AUTOFILL_FIELD_LABEL_STATE },
    { 8, ADDRESS_BILLING_ZIP,
      IDS_AUTOFILL_DIALOG_PLACEHOLDER_POSTAL_CODE, 0.5 },
    // TODO(estade): this should have a default based on the locale.
    { 9, ADDRESS_BILLING_COUNTRY, 0 },
    // TODO(ramankk): Add billing specific phone number.
    { 10, PHONE_HOME_WHOLE_NUMBER,
      IDS_AUTOFILL_DIALOG_PLACEHOLDER_PHONE_NUMBER },
  };

  const DetailInput kShippingInputs[] = {
    { 11, NAME_FULL, IDS_AUTOFILL_DIALOG_PLACEHOLDER_ADDRESSEE_NAME },
    { 12, ADDRESS_HOME_LINE1, IDS_AUTOFILL_DIALOG_PLACEHOLDER_ADDRESS_LINE_1 },
    { 13, ADDRESS_HOME_LINE2, IDS_AUTOFILL_DIALOG_PLACEHOLDER_ADDRESS_LINE_2 },
    { 14, ADDRESS_HOME_CITY, IDS_AUTOFILL_DIALOG_PLACEHOLDER_LOCALITY },
    { 15, ADDRESS_HOME_STATE, IDS_AUTOFILL_FIELD_LABEL_STATE },
    { 15, ADDRESS_HOME_ZIP, IDS_AUTOFILL_DIALOG_PLACEHOLDER_POSTAL_CODE, 0.5 },
    { 16, ADDRESS_HOME_COUNTRY, 0 },
    { 17, PHONE_HOME_WHOLE_NUMBER,
      IDS_AUTOFILL_DIALOG_PLACEHOLDER_PHONE_NUMBER },
  };

  BuildInputs(kEmailInputs,
              arraysize(kEmailInputs),
              &requested_email_fields_);

  BuildInputs(kCCInputs,
              arraysize(kCCInputs),
              &requested_cc_fields_);

  BuildInputs(kBillingInputs,
              arraysize(kBillingInputs),
              &requested_billing_fields_);

  BuildInputs(kCCInputs,
              arraysize(kCCInputs),
              &requested_cc_billing_fields_);
  BuildInputs(kBillingInputs,
              arraysize(kBillingInputs),
              &requested_cc_billing_fields_);

  BuildInputs(kShippingInputs,
              arraysize(kShippingInputs),
              &requested_shipping_fields_);

  SuggestionsUpdated();

  // TODO(estade): don't show the dialog if the site didn't specify the right
  // fields. First we must figure out what the "right" fields are.
  view_.reset(CreateView());
  view_->Show();
  GetManager()->AddObserver(this);

  // Try to see if the user is already signed-in.
  // If signed-in, fetch the user's Wallet data.
  // Otherwise, see if the user could be signed in passively.
  // TODO(aruslan): UMA metrics for sign-in.
  GetWalletItems();

  if (!account_chooser_model_.WalletIsSelected())
   LogDialogLatencyToShow();
}

void AutofillDialogControllerImpl::Hide() {
  if (view_)
    view_->Hide();
}

void AutofillDialogControllerImpl::UpdateProgressBar(double value) {
  view_->UpdateProgressBar(value);
}

bool AutofillDialogControllerImpl::AutocheckoutIsRunning() const {
  return autocheckout_state_ == AUTOCHECKOUT_IN_PROGRESS;
}

void AutofillDialogControllerImpl::OnAutocheckoutError() {
  DCHECK_EQ(AUTOCHECKOUT_IN_PROGRESS, autocheckout_state_);
  GetMetricLogger().LogAutocheckoutDuration(
      base::Time::Now() - autocheckout_started_timestamp_,
      AutofillMetrics::AUTOCHECKOUT_FAILED);
  autocheckout_state_ = AUTOCHECKOUT_ERROR;
  autocheckout_started_timestamp_ = base::Time();
  view_->UpdateNotificationArea();
  view_->UpdateButtonStrip();
  view_->UpdateDetailArea();
}

void AutofillDialogControllerImpl::OnAutocheckoutSuccess() {
  DCHECK_EQ(AUTOCHECKOUT_IN_PROGRESS, autocheckout_state_);
  GetMetricLogger().LogAutocheckoutDuration(
      base::Time::Now() - autocheckout_started_timestamp_,
      AutofillMetrics::AUTOCHECKOUT_SUCCEEDED);
  autocheckout_state_ = AUTOCHECKOUT_SUCCESS;
  autocheckout_started_timestamp_ = base::Time();
  view_->UpdateNotificationArea();
  view_->UpdateButtonStrip();
}

////////////////////////////////////////////////////////////////////////////////
// AutofillDialogController implementation.

string16 AutofillDialogControllerImpl::DialogTitle() const {
  return l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_TITLE);
}

string16 AutofillDialogControllerImpl::EditSuggestionText() const {
  return l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_EDIT);
}

string16 AutofillDialogControllerImpl::CancelButtonText() const {
  if (autocheckout_state_ == AUTOCHECKOUT_ERROR)
    return l10n_util::GetStringUTF16(IDS_OK);
  if (autocheckout_state_ == AUTOCHECKOUT_SUCCESS)
    return l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_CONTINUE_BUTTON);
  return l10n_util::GetStringUTF16(IDS_CANCEL);
}

string16 AutofillDialogControllerImpl::ConfirmButtonText() const {
  return l10n_util::GetStringUTF16(IsSubmitPausedOn(wallet::VERIFY_CVV) ?
      IDS_AUTOFILL_DIALOG_VERIFY_BUTTON : IDS_AUTOFILL_DIALOG_SUBMIT_BUTTON);
}

string16 AutofillDialogControllerImpl::SaveLocallyText() const {
  return l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_SAVE_LOCALLY_CHECKBOX);
}

string16 AutofillDialogControllerImpl::ProgressBarText() const {
  return l10n_util::GetStringUTF16(
      IDS_AUTOFILL_DIALOG_AUTOCHECKOUT_PROGRESS_BAR);
}

string16 AutofillDialogControllerImpl::LegalDocumentsText() {
  if (!IsPayingWithWallet())
    return string16();

  EnsureLegalDocumentsText();
  return legal_documents_text_;
}

DialogSignedInState AutofillDialogControllerImpl::SignedInState() const {
  if (account_chooser_model_.had_wallet_error())
    return SIGN_IN_DISABLED;

  if (signin_helper_ || !wallet_items_)
    return REQUIRES_RESPONSE;

  if (wallet_items_->HasRequiredAction(wallet::GAIA_AUTH))
    return REQUIRES_SIGN_IN;

  if (wallet_items_->HasRequiredAction(wallet::PASSIVE_GAIA_AUTH))
    return REQUIRES_PASSIVE_SIGN_IN;

  return SIGNED_IN;
}

bool AutofillDialogControllerImpl::ShouldShowSpinner() const {
  return account_chooser_model_.WalletIsSelected() &&
         SignedInState() == REQUIRES_RESPONSE;
}

string16 AutofillDialogControllerImpl::AccountChooserText() const {
  // TODO(aruslan): this should be l10n "Not using Google Wallet".
  if (!account_chooser_model_.WalletIsSelected())
    return l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_PAY_WITHOUT_WALLET);

  if (SignedInState() == SIGNED_IN)
    return account_chooser_model_.active_wallet_account_name();

  // In this case, the account chooser should be showing the signin link.
  return string16();
}

string16 AutofillDialogControllerImpl::SignInLinkText() const {
  return l10n_util::GetStringUTF16(
      signin_registrar_.IsEmpty() ? IDS_AUTOFILL_DIALOG_SIGN_IN :
                                    IDS_AUTOFILL_DIALOG_PAY_WITHOUT_WALLET);
}

bool AutofillDialogControllerImpl::ShouldOfferToSaveInChrome() const {
  // If Autocheckout is running, hide this checkbox so the progress bar has some
  // room. If Autocheckout had an error, neither the [X] Save details in chrome
  // nor the progress bar should show.
  return !IsPayingWithWallet() &&
      !profile_->IsOffTheRecord() &&
      IsManuallyEditingAnySection() &&
      !ShouldShowProgressBar() &&
      autocheckout_state_ != AUTOCHECKOUT_ERROR;
}

int AutofillDialogControllerImpl::GetDialogButtons() const {
  if (autocheckout_state_ != AUTOCHECKOUT_NOT_STARTED)
    return ui::DIALOG_BUTTON_CANCEL;
  return ui::DIALOG_BUTTON_OK | ui::DIALOG_BUTTON_CANCEL;
}

bool AutofillDialogControllerImpl::IsDialogButtonEnabled(
    ui::DialogButton button) const {
  if (button == ui::DIALOG_BUTTON_OK) {
    if (IsSubmitPausedOn(wallet::VERIFY_CVV))
      return true;
    if (is_submitting_ || ShouldShowSpinner())
      return false;
    return true;
  }

  DCHECK_EQ(ui::DIALOG_BUTTON_CANCEL, button);
  // TODO(ahutter): Make it possible for the user to cancel out of the dialog
  // while Autocheckout is in progress.
  return autocheckout_state_ != AUTOCHECKOUT_IN_PROGRESS ||
         !callback_.is_null();
}

const std::vector<ui::Range>& AutofillDialogControllerImpl::
    LegalDocumentLinks() {
  EnsureLegalDocumentsText();
  return legal_document_link_ranges_;
}

bool AutofillDialogControllerImpl::SectionIsActive(DialogSection section)
    const {
  if (IsSubmitPausedOn(wallet::VERIFY_CVV))
    return section == SECTION_CC_BILLING;

  if (IsPayingWithWallet())
    return section == SECTION_CC_BILLING || section == SECTION_SHIPPING;

  return section != SECTION_CC_BILLING;
}

bool AutofillDialogControllerImpl::HasCompleteWallet() const {
  return wallet_items_.get() != NULL &&
         !wallet_items_->instruments().empty() &&
         !wallet_items_->addresses().empty();
}

bool AutofillDialogControllerImpl::IsSubmitPausedOn(
    wallet::RequiredAction required_action) const {
  return full_wallet_ && full_wallet_->HasRequiredAction(required_action);
}

void AutofillDialogControllerImpl::GetWalletItems() {
  GetWalletClient()->GetWalletItems(source_url_);
}

void AutofillDialogControllerImpl::HideSignIn() {
  signin_registrar_.RemoveAll();
  view_->HideSignIn();
  view_->UpdateAccountChooser();
}

void AutofillDialogControllerImpl::SignedInStateUpdated() {
  switch (SignedInState()) {
    case SIGNED_IN:
      // Start fetching the user name if we don't know it yet.
      if (account_chooser_model_.active_wallet_account_name().empty()) {
        signin_helper_.reset(new wallet::WalletSigninHelper(
            this, profile_->GetRequestContext()));
        signin_helper_->StartUserNameFetch();
      } else {
        LogDialogLatencyToShow();
      }
      break;

    case REQUIRES_SIGN_IN:
    case SIGN_IN_DISABLED:
      // Switch to the local account and refresh the dialog.
      OnWalletSigninError();
      break;

    case REQUIRES_PASSIVE_SIGN_IN:
      // Attempt to passively sign in the user.
      DCHECK(!signin_helper_);
      account_chooser_model_.ClearActiveWalletAccountName();
      signin_helper_.reset(new wallet::WalletSigninHelper(
          this,
          profile_->GetRequestContext()));
      signin_helper_->StartPassiveSignin();
      break;

    case REQUIRES_RESPONSE:
      break;
  }
}

void AutofillDialogControllerImpl::OnWalletOrSigninUpdate() {
  SignedInStateUpdated();
  SuggestionsUpdated();
  UpdateAccountChooserView();

  if (view_)
    view_->UpdateButtonStrip();

  // On the first successful response, compute the initial user state metric.
  if (initial_user_state_ == AutofillMetrics::DIALOG_USER_STATE_UNKNOWN)
    initial_user_state_ = GetInitialUserState();
}

void AutofillDialogControllerImpl::OnWalletSigninError() {
  signin_helper_.reset();
  account_chooser_model_.SetHadWalletSigninError();
  GetWalletClient()->CancelRequests();
  LogDialogLatencyToShow();
}

void AutofillDialogControllerImpl::EnsureLegalDocumentsText() {
  if (!wallet_items_ || wallet_items_->legal_documents().empty())
    return;

  // The text has already been constructed, no need to recompute.
  if (!legal_documents_text_.empty())
    return;

  const std::vector<wallet::WalletItems::LegalDocument*>& documents =
      wallet_items_->legal_documents();
  DCHECK_LE(documents.size(), 3U);
  DCHECK_GE(documents.size(), 2U);
  const bool new_user = wallet_items_->HasRequiredAction(wallet::SETUP_WALLET);

  const string16 privacy_policy_display_name =
      l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_PRIVACY_POLICY_LINK);
  string16 text;
  if (documents.size() == 2U) {
    text = l10n_util::GetStringFUTF16(
        new_user ? IDS_AUTOFILL_DIALOG_LEGAL_LINKS_NEW_2 :
                   IDS_AUTOFILL_DIALOG_LEGAL_LINKS_UPDATED_2,
        documents[0]->display_name(),
        documents[1]->display_name());
  } else {
    text = l10n_util::GetStringFUTF16(
        new_user ? IDS_AUTOFILL_DIALOG_LEGAL_LINKS_NEW_3 :
                   IDS_AUTOFILL_DIALOG_LEGAL_LINKS_UPDATED_3,
        documents[0]->display_name(),
        documents[1]->display_name(),
        documents[2]->display_name());
  }

  legal_document_link_ranges_.clear();
  for (size_t i = 0; i < documents.size(); ++i) {
    size_t link_start = text.find(documents[i]->display_name());
    legal_document_link_ranges_.push_back(ui::Range(
        link_start, link_start + documents[i]->display_name().size()));
  }
  legal_documents_text_ = text;
}

void AutofillDialogControllerImpl::PrepareDetailInputsForSection(
    DialogSection section) {
  // Reset all previously entered data and stop editing |section|.
  DetailInputs* inputs = MutableRequestedFieldsForSection(section);
  for (size_t i = 0; i < inputs->size(); ++i) {
    (*inputs)[i].initial_value.clear();
  }
  section_editing_state_[section] = false;

  // If the chosen item in |model| yields an empty suggestion text, it is
  // invalid. In this case, show the editing UI with invalid fields highlighted.
  SuggestionsMenuModel* model = SuggestionsMenuModelForSection(section);
  if (IsASuggestionItemKey(model->GetItemKeyForCheckedItem()) &&
      SuggestionTextForSection(section).empty()) {
    scoped_ptr<DataModelWrapper> wrapper = CreateWrapper(section);
    wrapper->FillInputs(MutableRequestedFieldsForSection(section));
    section_editing_state_[section] = true;
  }

  if (view_)
    view_->UpdateSection(section);
}

const DetailInputs& AutofillDialogControllerImpl::RequestedFieldsForSection(
    DialogSection section) const {
  switch (section) {
    case SECTION_EMAIL:
      return requested_email_fields_;
    case SECTION_CC:
      return requested_cc_fields_;
    case SECTION_BILLING:
      return requested_billing_fields_;
    case SECTION_CC_BILLING:
      return requested_cc_billing_fields_;
    case SECTION_SHIPPING:
      return requested_shipping_fields_;
  }

  NOTREACHED();
  return requested_billing_fields_;
}

ui::ComboboxModel* AutofillDialogControllerImpl::ComboboxModelForAutofillType(
    AutofillFieldType type) {
  switch (AutofillType::GetEquivalentFieldType(type)) {
    case CREDIT_CARD_EXP_MONTH:
      return &cc_exp_month_combobox_model_;

    case CREDIT_CARD_EXP_4_DIGIT_YEAR:
      return &cc_exp_year_combobox_model_;

    case ADDRESS_HOME_COUNTRY:
      return &country_combobox_model_;

    default:
      return NULL;
  }
}

ui::MenuModel* AutofillDialogControllerImpl::MenuModelForSection(
    DialogSection section) {
  SuggestionsMenuModel* model = SuggestionsMenuModelForSection(section);
  // The shipping section menu is special. It will always show because there is
  // a choice between "Use billing" and "enter new".
  if (section == SECTION_SHIPPING)
    return model;

  // For other sections, only show a menu if there's at least one suggestion.
  for (int i = 0; i < model->GetItemCount(); ++i) {
    if (IsASuggestionItemKey(model->GetItemKeyAt(i)))
      return model;
  }

  return NULL;
}

#if defined(OS_ANDROID)
ui::MenuModel* AutofillDialogControllerImpl::MenuModelForSectionHack(
    DialogSection section) {
  return SuggestionsMenuModelForSection(section);
}
#endif

ui::MenuModel* AutofillDialogControllerImpl::MenuModelForAccountChooser() {
  // If there were unrecoverable Wallet errors, or if there are choices other
  // than "Pay without the wallet", show the full menu.
  if (account_chooser_model_.had_wallet_error() ||
      account_chooser_model_.HasAccountsToChoose()) {
    return &account_chooser_model_;
  }

  // Otherwise, there is no menu, just a sign in link.
  return NULL;
}

gfx::Image AutofillDialogControllerImpl::AccountChooserImage() {
  if (!MenuModelForAccountChooser()) {
    if (signin_registrar_.IsEmpty()) {
      return ui::ResourceBundle::GetSharedInstance().GetImageNamed(
          IDR_WALLET_ICON);
    }

    return gfx::Image();
  }

  gfx::Image icon;
  account_chooser_model_.GetIconAt(
      account_chooser_model_.GetIndexOfCommandId(
          account_chooser_model_.checked_item()),
      &icon);
  return icon;
}

bool AutofillDialogControllerImpl::ShouldShowDetailArea() const {
  // Hide the detail area when Autocheckout is running or there was an error (as
  // there's nothing they can do after an error but cancel).
  return autocheckout_state_ == AUTOCHECKOUT_NOT_STARTED;
}

bool AutofillDialogControllerImpl::ShouldShowProgressBar() const {
  // Show the progress bar while Autocheckout is running but hide it on errors,
  // as there's no use leaving it up if the flow has failed.
  return autocheckout_state_ == AUTOCHECKOUT_IN_PROGRESS;
}

string16 AutofillDialogControllerImpl::LabelForSection(DialogSection section)
    const {
  switch (section) {
    case SECTION_EMAIL:
      return l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_SECTION_EMAIL);
    case SECTION_CC:
      return l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_SECTION_CC);
    case SECTION_BILLING:
      return l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_SECTION_BILLING);
    case SECTION_CC_BILLING:
      return l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_SECTION_CC_BILLING);
    case SECTION_SHIPPING:
      return l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_SECTION_SHIPPING);
    default:
      NOTREACHED();
      return string16();
  }
}

SuggestionState AutofillDialogControllerImpl::SuggestionStateForSection(
    DialogSection section) {
  return SuggestionState(SuggestionTextForSection(section),
                         SuggestionTextStyleForSection(section),
                         SuggestionIconForSection(section),
                         ExtraSuggestionTextForSection(section),
                         ExtraSuggestionIconForSection(section),
                         EditEnabledForSection(section));
}

string16 AutofillDialogControllerImpl::SuggestionTextForSection(
    DialogSection section) {
  string16 action_text = RequiredActionTextForSection(section);
  if (!action_text.empty())
    return action_text;

  // When the user has clicked 'edit' or a suggestion is somehow invalid (e.g. a
  // user selects a credit card that has expired), don't show a suggestion (even
  // though there is a profile selected in the model).
  if (section_editing_state_[section])
    return string16();

  SuggestionsMenuModel* model = SuggestionsMenuModelForSection(section);
  std::string item_key = model->GetItemKeyForCheckedItem();
  if (item_key == kSameAsBillingKey) {
    return l10n_util::GetStringUTF16(
        IDS_AUTOFILL_DIALOG_USING_BILLING_FOR_SHIPPING);
  }

  if (!IsASuggestionItemKey(item_key))
    return string16();

  if (section == SECTION_EMAIL)
    return model->GetLabelAt(model->checked_item());

  scoped_ptr<DataModelWrapper> wrapper = CreateWrapper(section);
  return wrapper->GetDisplayText();
}

gfx::Font::FontStyle
    AutofillDialogControllerImpl::SuggestionTextStyleForSection(
        DialogSection section) const {
  const SuggestionsMenuModel* model = SuggestionsMenuModelForSection(section);
  if (model->GetItemKeyForCheckedItem() == kSameAsBillingKey)
    return gfx::Font::ITALIC;

  return gfx::Font::NORMAL;
}

string16 AutofillDialogControllerImpl::RequiredActionTextForSection(
    DialogSection section) const {
  if (section == SECTION_CC_BILLING && IsSubmitPausedOn(wallet::VERIFY_CVV)) {
    const wallet::WalletItems::MaskedInstrument* current_instrument =
        wallet_items_->GetInstrumentById(active_instrument_id_);
    if (current_instrument)
      return current_instrument->TypeAndLastFourDigits();

    DetailOutputMap output;
    view_->GetUserInput(section, &output);
    CreditCard card;
    GetBillingInfoFromOutputs(output, &card, NULL, NULL);
    return card.TypeAndLastFourDigits();
  }

  return string16();
}

string16 AutofillDialogControllerImpl::ExtraSuggestionTextForSection(
    DialogSection section) const {
  if (section == SECTION_CC ||
      (section == SECTION_CC_BILLING && IsSubmitPausedOn(wallet::VERIFY_CVV))) {
    return l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_PLACEHOLDER_CVC);
  }

  return string16();
}

scoped_ptr<DataModelWrapper> AutofillDialogControllerImpl::CreateWrapper(
    DialogSection section) {
  if (IsPayingWithWallet() && full_wallet_ &&
      full_wallet_->required_actions().empty()) {
    if (section == SECTION_CC_BILLING) {
      return scoped_ptr<DataModelWrapper>(
          new FullWalletBillingWrapper(full_wallet_.get()));
    }
    if (section == SECTION_SHIPPING) {
      return scoped_ptr<DataModelWrapper>(
          new FullWalletShippingWrapper(full_wallet_.get()));
    }
  }

  SuggestionsMenuModel* model = SuggestionsMenuModelForSection(section);
  std::string item_key = model->GetItemKeyForCheckedItem();
  if (!IsASuggestionItemKey(item_key) || IsManuallyEditingSection(section))
    return scoped_ptr<DataModelWrapper>();

  if (IsPayingWithWallet()) {
    int index;
    bool success = base::StringToInt(item_key, &index);
    DCHECK(success);

    if (section == SECTION_CC_BILLING) {
      return scoped_ptr<DataModelWrapper>(
          new WalletInstrumentWrapper(wallet_items_->instruments()[index]));
    }

    if (section == SECTION_SHIPPING) {
      return scoped_ptr<DataModelWrapper>(
          new WalletAddressWrapper(wallet_items_->addresses()[index]));
    }

    return scoped_ptr<DataModelWrapper>();
  }

  if (section == SECTION_CC) {
    CreditCard* card = GetManager()->GetCreditCardByGUID(item_key);
    DCHECK(card);
    return scoped_ptr<DataModelWrapper>(new AutofillCreditCardWrapper(card));
  }

  // Calculate the variant by looking at how many items come from the same
  // data model.
  size_t variant = 0;
  for (int i = model->checked_item() - 1; i >= 0; --i) {
    if (model->GetItemKeyAt(i) == item_key)
      variant++;
    else
      break;
  }

  AutofillProfile* profile = GetManager()->GetProfileByGUID(item_key);
  DCHECK(profile);
  return scoped_ptr<DataModelWrapper>(
      new AutofillProfileWrapper(profile, variant));
}

gfx::Image AutofillDialogControllerImpl::SuggestionIconForSection(
    DialogSection section) {
  scoped_ptr<DataModelWrapper> model = CreateWrapper(section);
  if (!model.get())
    return gfx::Image();

  return model->GetIcon();
}

gfx::Image AutofillDialogControllerImpl::ExtraSuggestionIconForSection(
    DialogSection section) const {
  if (section == SECTION_CC || section == SECTION_CC_BILLING)
    return IconForField(CREDIT_CARD_VERIFICATION_CODE, string16());

  return gfx::Image();
}

bool AutofillDialogControllerImpl::EditEnabledForSection(
    DialogSection section) const {
  if (SuggestionsMenuModelForSection(section)->GetItemKeyForCheckedItem() ==
      kSameAsBillingKey) {
    return false;
  }

  if (section == SECTION_CC_BILLING && IsSubmitPausedOn(wallet::VERIFY_CVV))
    return false;

  return true;
}

void AutofillDialogControllerImpl::EditClickedForSection(
    DialogSection section) {
  scoped_ptr<DataModelWrapper> model = CreateWrapper(section);
  model->FillInputs(MutableRequestedFieldsForSection(section));
  section_editing_state_[section] = true;
  view_->UpdateSection(section);

  GetMetricLogger().LogDialogUiEvent(
      GetDialogType(), DialogSectionToUiEditEvent(section));
}

void AutofillDialogControllerImpl::EditCancelledForSection(
    DialogSection section) {
  PrepareDetailInputsForSection(section);
}

gfx::Image AutofillDialogControllerImpl::IconForField(
    AutofillFieldType type, const string16& user_input) const {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  if (type == CREDIT_CARD_VERIFICATION_CODE)
    return rb.GetImageNamed(IDR_CREDIT_CARD_CVC_HINT);

  // For the credit card, we show a few grayscale images, and possibly one
  // color image if |user_input| is a valid card number.
  if (type == CREDIT_CARD_NUMBER) {
    const int card_idrs[] = {
      IDR_AUTOFILL_CC_VISA,
      IDR_AUTOFILL_CC_MASTERCARD,
      IDR_AUTOFILL_CC_AMEX,
      IDR_AUTOFILL_CC_DISCOVER
    };
    const int number_of_cards = arraysize(card_idrs);
    // The number of pixels between card icons.
    const int kCardPadding = 2;

    gfx::ImageSkia some_card = *rb.GetImageSkiaNamed(card_idrs[0]);
    const int card_width = some_card.width();
    gfx::Canvas canvas(
        gfx::Size((card_width + kCardPadding) * number_of_cards - kCardPadding,
                  some_card.height()),
        ui::SCALE_FACTOR_100P,
        false);
    CreditCard card;
    card.SetRawInfo(CREDIT_CARD_NUMBER, user_input);

    for (int i = 0; i < number_of_cards; ++i) {
      int idr = card_idrs[i];
      gfx::ImageSkia card_image = *rb.GetImageSkiaNamed(idr);
      if (card.IconResourceId() != idr) {
        SkBitmap disabled_bitmap =
            SkBitmapOperations::CreateHSLShiftedBitmap(*card_image.bitmap(),
                                                       kGrayImageShift);
        card_image = gfx::ImageSkia::CreateFrom1xBitmap(disabled_bitmap);
      }

      canvas.DrawImageInt(card_image, i * (card_width + kCardPadding), 0);
    }

    gfx::ImageSkia skia(canvas.ExtractImageRep());
    return gfx::Image(skia);
  }

  return gfx::Image();
}

// TODO(estade): Replace all the error messages here with more helpful and
// translateable ones. TODO(groby): Also add tests.
string16 AutofillDialogControllerImpl::InputValidityMessage(
    AutofillFieldType type,
    const string16& value) const {
  if (InputIsValid(type, value))
    return string16();

  if (value.empty())
    return ASCIIToUTF16("You forgot one");

  return ASCIIToUTF16("Are you sure this is right?");
}

// TODO(estade): Replace all the error messages here with more helpful and
// translateable ones. TODO(groby): Also add tests.
ValidityData AutofillDialogControllerImpl::InputsAreValid(
    const DetailOutputMap& inputs, ValidationType validation_type) const {
  ValidityData invalid_messages;
  std::map<AutofillFieldType, string16> field_values;
  for (DetailOutputMap::const_iterator iter = inputs.begin();
       iter != inputs.end(); ++iter) {
    // Skip empty fields in edit mode.
    if (validation_type == VALIDATE_EDIT && iter->second.empty())
      continue;

    const AutofillFieldType type = iter->first->type;
    string16 message = InputValidityMessage(type, iter->second);
    if (!message.empty())
      invalid_messages[type] = message;
    else
      field_values[type] = iter->second;
  }

  // Validate the date formed by month and year field. (Autofill dialog is
  // never supposed to have 2-digit years, so not checked).
  if (field_values.count(CREDIT_CARD_EXP_MONTH) &&
      field_values.count(CREDIT_CARD_EXP_4_DIGIT_YEAR)) {
    if (!autofill::IsValidCreditCardExpirationDate(
            field_values[CREDIT_CARD_EXP_4_DIGIT_YEAR],
            field_values[CREDIT_CARD_EXP_MONTH],
            base::Time::Now())) {
      invalid_messages[CREDIT_CARD_EXP_MONTH] =
          ASCIIToUTF16("more complicated message");
      invalid_messages[CREDIT_CARD_EXP_4_DIGIT_YEAR] =
          ASCIIToUTF16("more complicated message");
    }
  }

  // If there is a credit card number and a CVC, validate them together.
  if (field_values.count(CREDIT_CARD_NUMBER) &&
      field_values.count(CREDIT_CARD_VERIFICATION_CODE) &&
      InputIsValid(CREDIT_CARD_NUMBER, field_values[CREDIT_CARD_NUMBER])) {
    if (!autofill::IsValidCreditCardSecurityCode(
            field_values[CREDIT_CARD_VERIFICATION_CODE],
            field_values[CREDIT_CARD_NUMBER])) {
      invalid_messages[CREDIT_CARD_VERIFICATION_CODE] =
          ASCIIToUTF16("CVC doesn't match card type!");
    }
  }

  // Validate the phone number against the country code of the address.
  if (field_values.count(ADDRESS_HOME_COUNTRY) &&
      field_values.count(PHONE_HOME_WHOLE_NUMBER)) {
    i18n::PhoneObject phone_object(
        field_values[PHONE_HOME_WHOLE_NUMBER],
        AutofillCountry::GetCountryCode(
            field_values[ADDRESS_HOME_COUNTRY],
            g_browser_process->GetApplicationLocale()));
    if (!phone_object.IsValidNumber()) {
      invalid_messages[PHONE_HOME_WHOLE_NUMBER] =
          ASCIIToUTF16("Invalid phone number");
    }
  }

  return invalid_messages;
}

void AutofillDialogControllerImpl::UserEditedOrActivatedInput(
    const DetailInput* input,
    gfx::NativeView parent_view,
    const gfx::Rect& content_bounds,
    const string16& field_contents,
    bool was_edit) {
  // If the field is edited down to empty, don't show a popup.
  if (was_edit && field_contents.empty()) {
    HidePopup();
    return;
  }

  // If the user clicks while the popup is already showing, be sure to hide
  // it.
  if (!was_edit && popup_controller_) {
    HidePopup();
    return;
  }

  std::vector<string16> popup_values, popup_labels, popup_icons;
  if (IsCreditCardType(input->type)) {
    GetManager()->GetCreditCardSuggestions(input->type,
                                           field_contents,
                                           &popup_values,
                                           &popup_labels,
                                           &popup_icons,
                                           &popup_guids_);
  } else {
    std::vector<AutofillFieldType> field_types;
    field_types.push_back(EMAIL_ADDRESS);
    for (DetailInputs::const_iterator iter = requested_shipping_fields_.begin();
         iter != requested_shipping_fields_.end(); ++iter) {
      field_types.push_back(iter->type);
    }
    GetManager()->GetProfileSuggestions(input->type,
                                        field_contents,
                                        false,
                                        field_types,
                                        &popup_values,
                                        &popup_labels,
                                        &popup_icons,
                                        &popup_guids_);
  }

  if (popup_values.empty()) {
    HidePopup();
    return;
  }

  // TODO(estade): do we need separators and control rows like 'Clear
  // Form'?
  std::vector<int> popup_ids;
  for (size_t i = 0; i < popup_guids_.size(); ++i) {
    popup_ids.push_back(i);
  }

  popup_controller_ = AutofillPopupControllerImpl::GetOrCreate(
      popup_controller_,
      weak_ptr_factory_.GetWeakPtr(),
      parent_view,
      content_bounds);
  popup_controller_->Show(popup_values,
                          popup_labels,
                          popup_icons,
                          popup_ids);
  input_showing_popup_ = input;
}

void AutofillDialogControllerImpl::FocusMoved() {
  HidePopup();
}

void AutofillDialogControllerImpl::ViewClosed() {
  GetManager()->RemoveObserver(this);

  // TODO(ahutter): Once a user can cancel Autocheckout mid-flow, log that
  // metric here.

  delete this;
}

std::vector<DialogNotification>
    AutofillDialogControllerImpl::CurrentNotifications() const {
  std::vector<DialogNotification> notifications;

  if (account_chooser_model_.had_wallet_error()) {
    // TODO(dbeam): pass along the Wallet error or remove from the translation.
    // TODO(dbeam): figure out a way to dismiss this error after a while.
    notifications.push_back(DialogNotification(
        DialogNotification::WALLET_ERROR,
        l10n_util::GetStringFUTF16(IDS_AUTOFILL_DIALOG_COMPLETE_WITHOUT_WALLET,
                                   ASCIIToUTF16("[Wallet-Error]."))));
  } else {
    if (IsFirstRun()) {
      if (SignedInState() == SIGNED_IN) {
        if (account_chooser_model_.WalletIsSelected() && HasCompleteWallet()) {
          // First run, signed in, has a complete Google Wallet.
          notifications.push_back(DialogNotification(
              DialogNotification::EXPLANATORY_MESSAGE,
              l10n_util::GetStringUTF16(
                  IDS_AUTOFILL_DIALOG_DETAILS_FROM_WALLET)));
        } else {
          // First run, signed in, has an incomplete (or no) Google Wallet.
          DialogNotification notification(
              DialogNotification::WALLET_USAGE_CONFIRMATION,
              l10n_util::GetStringUTF16(
                  IDS_AUTOFILL_DIALOG_SAVE_DETAILS_IN_WALLET));
          notification.set_checked(account_chooser_model_.WalletIsSelected());
          notification.set_interactive(!is_submitting_);
          notifications.push_back(notification);
        }
      } else if (account_chooser_model_.WalletIsSelected()) {
        // First run, not signed in, wallet promo.
        notifications.push_back(DialogNotification(
            DialogNotification::WALLET_SIGNIN_PROMO,
            l10n_util::GetStringUTF16(
                IDS_AUTOFILL_DIALOG_SIGN_IN_AND_SAVE_DETAILS)));
      }
    } else if (SignedInState() == SIGNED_IN && !HasCompleteWallet()) {
      // After first run, signed in.
      DialogNotification notification(
          DialogNotification::WALLET_USAGE_CONFIRMATION,
          l10n_util::GetStringUTF16(
              IDS_AUTOFILL_DIALOG_SAVE_DETAILS_IN_WALLET));
      notification.set_checked(account_chooser_model_.WalletIsSelected());
      notification.set_interactive(!is_submitting_);
      notifications.push_back(notification);
    } else {
      // If the user isn't signed in and it's after the first run, no promo.
    }
  }

  if (RequestingCreditCardInfo() && !TransmissionWillBeSecure()) {
    notifications.push_back(DialogNotification(
        DialogNotification::SECURITY_WARNING,
        l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_SECURITY_WARNING)));
  }

  if (!invoked_from_same_origin_) {
    notifications.push_back(DialogNotification(
        DialogNotification::SECURITY_WARNING,
        l10n_util::GetStringFUTF16(IDS_AUTOFILL_DIALOG_SITE_WARNING,
                                   UTF8ToUTF16(source_url_.host()))));
  }

  if (IsSubmitPausedOn(wallet::VERIFY_CVV)) {
    notifications.push_back(DialogNotification(
            DialogNotification::REQUIRED_ACTION,
            l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_VERIFY_CVV)));
  }

  if (autocheckout_state_ == AUTOCHECKOUT_ERROR) {
    notifications.push_back(DialogNotification(
        DialogNotification::AUTOCHECKOUT_ERROR,
        l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_AUTOCHECKOUT_ERROR)));
  }

  if (autocheckout_state_ == AUTOCHECKOUT_SUCCESS) {
    notifications.push_back(DialogNotification(
        DialogNotification::AUTOCHECKOUT_SUCCESS,
        l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_AUTOCHECKOUT_SUCCESS)));
  }

  if (wallet_server_validation_error_) {
    // TODO(ahutter): L10n and UI.
    notifications.push_back(DialogNotification(
        DialogNotification::REQUIRED_ACTION,
        ASCIIToUTF16("New data failed validation on server side")));
  }

  return notifications;
}

void AutofillDialogControllerImpl::SignInLinkClicked() {
  if (signin_registrar_.IsEmpty()) {
    // Start sign in.
    DCHECK(!IsPayingWithWallet());

    content::Source<content::NavigationController> source(view_->ShowSignIn());
    signin_registrar_.Add(
        this, content::NOTIFICATION_NAV_ENTRY_COMMITTED, source);
    view_->UpdateAccountChooser();

    GetMetricLogger().LogDialogUiEvent(
        GetDialogType(), AutofillMetrics::DIALOG_UI_SIGNIN_SHOWN);
  } else {
    HideSignIn();
  }
}

void AutofillDialogControllerImpl::NotificationCheckboxStateChanged(
    DialogNotification::Type type, bool checked) {
  if (type == DialogNotification::WALLET_USAGE_CONFIRMATION) {
    if (checked)
      account_chooser_model_.SelectActiveWalletAccount();
    else
      account_chooser_model_.SelectUseAutofill();
  }
}

void AutofillDialogControllerImpl::LegalDocumentLinkClicked(
    const ui::Range& range) {
  for (size_t i = 0; i < legal_document_link_ranges_.size(); ++i) {
    if (legal_document_link_ranges_[i] == range) {
      OpenTabWithUrl(wallet_items_->legal_documents()[i]->url());
      return;
    }
  }

  NOTREACHED();
}

void AutofillDialogControllerImpl::OnCancel() {
  HidePopup();

  // If the submit was successful, |callback_| will have already been |.Run()|
  // and nullified. If this is the case, no further actions are required. If
  // Autocheckout has an error, it's possible that the dialog will be submitted
  // to start the flow and then cancelled to close the dialog after the error.
  if (callback_.is_null())
    return;

  LogOnCancelMetrics();

  callback_.Run(NULL, std::string());
  callback_ = base::Callback<void(const FormStructure*, const std::string&)>();
}

void AutofillDialogControllerImpl::OnAccept() {
  HidePopup();
  SetIsSubmitting(true);
  if (IsSubmitPausedOn(wallet::VERIFY_CVV)) {
    DCHECK(!active_instrument_id_.empty());
    GetWalletClient()->AuthenticateInstrument(
        active_instrument_id_,
        UTF16ToUTF8(view_->GetCvc()),
        wallet_items_->obfuscated_gaia_id());
  } else if (IsPayingWithWallet()) {
    SubmitWithWallet();
  } else {
    FinishSubmit();
  }
}

Profile* AutofillDialogControllerImpl::profile() {
  return profile_;
}

content::WebContents* AutofillDialogControllerImpl::web_contents() {
  return contents_;
}

////////////////////////////////////////////////////////////////////////////////
// AutofillPopupDelegate implementation.

void AutofillDialogControllerImpl::OnPopupShown(
    content::KeyboardListener* listener) {
  GetMetricLogger().LogDialogPopupEvent(
      GetDialogType(), AutofillMetrics::DIALOG_POPUP_SHOWN);
}

void AutofillDialogControllerImpl::OnPopupHidden(
    content::KeyboardListener* listener) {}

void AutofillDialogControllerImpl::DidSelectSuggestion(int identifier) {
  // TODO(estade): implement.
}

void AutofillDialogControllerImpl::DidAcceptSuggestion(const string16& value,
                                                       int identifier) {
  const PersonalDataManager::GUIDPair& pair = popup_guids_[identifier];

  scoped_ptr<DataModelWrapper> wrapper;
  if (IsCreditCardType(input_showing_popup_->type)) {
    wrapper.reset(new AutofillCreditCardWrapper(
        GetManager()->GetCreditCardByGUID(pair.first)));
  } else {
    wrapper.reset(new AutofillProfileWrapper(
        GetManager()->GetProfileByGUID(pair.first), pair.second));
  }

  for (size_t i = SECTION_MIN; i <= SECTION_MAX; ++i) {
    DialogSection section = static_cast<DialogSection>(i);
    wrapper->FillInputs(MutableRequestedFieldsForSection(section));
    view_->FillSection(section, *input_showing_popup_);
  }

  GetMetricLogger().LogDialogPopupEvent(
      GetDialogType(), AutofillMetrics::DIALOG_POPUP_FORM_FILLED);

  // TODO(estade): not sure why it's necessary to do this explicitly.
  HidePopup();
}

void AutofillDialogControllerImpl::RemoveSuggestion(const string16& value,
                                                    int identifier) {
  // TODO(estade): implement.
}

void AutofillDialogControllerImpl::ClearPreviewedForm() {
  // TODO(estade): implement.
}

////////////////////////////////////////////////////////////////////////////////
// content::NotificationObserver implementation.

void AutofillDialogControllerImpl::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(type, content::NOTIFICATION_NAV_ENTRY_COMMITTED);
  content::LoadCommittedDetails* load_details =
      content::Details<content::LoadCommittedDetails>(details).ptr();
  if (wallet::IsSignInContinueUrl(load_details->entry->GetVirtualURL())) {
    HideSignIn();
    account_chooser_model_.SelectActiveWalletAccount();
    GetWalletItems();
  }
}

////////////////////////////////////////////////////////////////////////////////
// SuggestionsMenuModelDelegate implementation.

void AutofillDialogControllerImpl::SuggestionItemSelected(
    SuggestionsMenuModel* model,
    size_t index) {
  if (model->GetItemKeyAt(index) == kManageItemsKey) {
    GURL url;
    if (!IsPayingWithWallet()) {
      GURL settings_url(chrome::kChromeUISettingsURL);
      url = settings_url.Resolve(chrome::kAutofillSubPage);
    } else {
      url = SectionForSuggestionsMenuModel(*model) == SECTION_SHIPPING ?
          wallet::GetManageAddressesUrl() : wallet::GetManageInstrumentsUrl();
    }

    OpenTabWithUrl(url);
    return;
  }

  model->SetCheckedIndex(index);
  PrepareDetailInputsForSection(SectionForSuggestionsMenuModel(*model));

  LogSuggestionItemSelectedMetric(*model);
}

////////////////////////////////////////////////////////////////////////////////
// wallet::WalletClientDelegate implementation.

const AutofillMetrics& AutofillDialogControllerImpl::GetMetricLogger() const {
  return metric_logger_;
}

DialogType AutofillDialogControllerImpl::GetDialogType() const {
  return dialog_type_;
}

std::string AutofillDialogControllerImpl::GetRiskData() const {
  // TODO(dbeam): Implement this.
  return "risky business";
}

void AutofillDialogControllerImpl::OnDidAcceptLegalDocuments() {
  // TODO(dbeam): Don't send risk params until legal documents are accepted:
  // http://crbug.com/173505
}

void AutofillDialogControllerImpl::OnDidAuthenticateInstrument(bool success) {
  DCHECK(is_submitting_ && IsPayingWithWallet());

  // TODO(dbeam): use the returned full wallet. b/8332329
  if (success)
    GetFullWallet();
  else
    DisableWallet();
}

void AutofillDialogControllerImpl::OnDidGetFullWallet(
    scoped_ptr<wallet::FullWallet> full_wallet) {
  DCHECK(is_submitting_ && IsPayingWithWallet());

  full_wallet_ = full_wallet.Pass();

  if (full_wallet_->required_actions().empty()) {
    FinishSubmit();
    return;
  }

  SuggestionsUpdated();
  view_->UpdateNotificationArea();
  view_->UpdateButtonStrip();
}

void AutofillDialogControllerImpl::OnPassiveSigninSuccess(
    const std::string& username) {
  const string16 username16 = UTF8ToUTF16(username);
  signin_helper_.reset();
  account_chooser_model_.SetActiveWalletAccountName(username16);
  GetWalletItems();
}

void AutofillDialogControllerImpl::OnUserNameFetchSuccess(
    const std::string& username) {
  const string16 username16 = UTF8ToUTF16(username);
  signin_helper_.reset();
  account_chooser_model_.SetActiveWalletAccountName(username16);
  OnWalletOrSigninUpdate();
}

void AutofillDialogControllerImpl::OnAutomaticSigninSuccess(
    const std::string& username) {
  NOTIMPLEMENTED();
}

void AutofillDialogControllerImpl::OnPassiveSigninFailure(
    const GoogleServiceAuthError& error) {
  // TODO(aruslan): report an error.
  LOG(ERROR) << "failed to passively sign in: " << error.ToString();
  OnWalletSigninError();
}

void AutofillDialogControllerImpl::OnUserNameFetchFailure(
    const GoogleServiceAuthError& error) {
  // TODO(aruslan): report an error.
  LOG(ERROR) << "failed to fetch the user account name: " << error.ToString();
  OnWalletSigninError();
}

void AutofillDialogControllerImpl::OnAutomaticSigninFailure(
    const GoogleServiceAuthError& error) {
  // TODO(aruslan): report an error.
  LOG(ERROR) << "failed to automatically sign in: " << error.ToString();
  OnWalletSigninError();
}

void AutofillDialogControllerImpl::OnDidGetWalletItems(
    scoped_ptr<wallet::WalletItems> wallet_items) {
  legal_documents_text_.clear();
  legal_document_link_ranges_.clear();

  // TODO(dbeam): verify items support kCartCurrency? http://crbug.com/232952
  wallet_items_ = wallet_items.Pass();
  OnWalletOrSigninUpdate();
}

void AutofillDialogControllerImpl::OnDidSaveAddress(
    const std::string& address_id,
    const std::vector<wallet::RequiredAction>& required_actions) {
  DCHECK(is_submitting_ && IsPayingWithWallet());

  if (required_actions.empty()) {
    active_address_id_ = address_id;
    if (!active_instrument_id_.empty())
      GetFullWallet();
  } else {
    HandleSaveOrUpdateRequiredActions(required_actions);
  }
}

void AutofillDialogControllerImpl::OnDidSaveInstrument(
    const std::string& instrument_id,
    const std::vector<wallet::RequiredAction>& required_actions) {
  DCHECK(is_submitting_ && IsPayingWithWallet());

  if (required_actions.empty()) {
    active_instrument_id_ = instrument_id;
    if (!active_address_id_.empty())
      GetFullWallet();
  } else {
    HandleSaveOrUpdateRequiredActions(required_actions);
  }
}

void AutofillDialogControllerImpl::OnDidSaveInstrumentAndAddress(
    const std::string& instrument_id,
    const std::string& address_id,
    const std::vector<wallet::RequiredAction>& required_actions) {
  OnDidSaveInstrument(instrument_id, required_actions);
  OnDidSaveAddress(address_id, required_actions);
}

void AutofillDialogControllerImpl::OnDidUpdateAddress(
    const std::string& address_id,
    const std::vector<wallet::RequiredAction>& required_actions) {
  OnDidSaveAddress(address_id, required_actions);
}

void AutofillDialogControllerImpl::OnDidUpdateInstrument(
    const std::string& instrument_id,
    const std::vector<wallet::RequiredAction>& required_actions) {
  OnDidSaveInstrument(instrument_id, required_actions);
}

void AutofillDialogControllerImpl::OnWalletError(
    wallet::WalletClient::ErrorType error_type) {
  // TODO(dbeam): Do something with |error_type|. http://crbug.com/164410
  DisableWallet();
}

void AutofillDialogControllerImpl::OnMalformedResponse() {
  DisableWallet();
}

void AutofillDialogControllerImpl::OnNetworkError(int response_code) {
  DisableWallet();
}

////////////////////////////////////////////////////////////////////////////////
// PersonalDataManagerObserver implementation.

void AutofillDialogControllerImpl::OnPersonalDataChanged() {
  SuggestionsUpdated();
}

////////////////////////////////////////////////////////////////////////////////
// AccountChooserModelDelegate implementation.

void AutofillDialogControllerImpl::AccountChoiceChanged() {
  if (is_submitting_)
    GetWalletClient()->CancelRequests();

  SetIsSubmitting(false);

  SuggestionsUpdated();
  UpdateAccountChooserView();
}

void AutofillDialogControllerImpl::UpdateAccountChooserView() {
  if (view_) {
    view_->UpdateAccountChooser();
    view_->UpdateNotificationArea();
  }
}

////////////////////////////////////////////////////////////////////////////////

bool AutofillDialogControllerImpl::HandleKeyPressEventInInput(
    const content::NativeWebKeyboardEvent& event) {
  if (popup_controller_)
    return popup_controller_->HandleKeyPressEvent(event);

  return false;
}

bool AutofillDialogControllerImpl::RequestingCreditCardInfo() const {
  DCHECK_GT(form_structure_.field_count(), 0U);

  for (size_t i = 0; i < form_structure_.field_count(); ++i) {
    if (IsCreditCardType(form_structure_.field(i)->type()))
      return true;
  }

  return false;
}

bool AutofillDialogControllerImpl::TransmissionWillBeSecure() const {
  return source_url_.SchemeIs(chrome::kHttpsScheme) &&
         !net::IsCertStatusError(ssl_status_.cert_status) &&
         !net::IsCertStatusMinorError(ssl_status_.cert_status);
}

AutofillDialogControllerImpl::AutofillDialogControllerImpl(
    content::WebContents* contents,
    const FormData& form_structure,
    const GURL& source_url,
    const DialogType dialog_type,
    const base::Callback<void(const FormStructure*,
                              const std::string&)>& callback)
    : profile_(Profile::FromBrowserContext(contents->GetBrowserContext())),
      contents_(contents),
      initial_user_state_(AutofillMetrics::DIALOG_USER_STATE_UNKNOWN),
      dialog_type_(dialog_type),
      form_structure_(form_structure, std::string()),
      invoked_from_same_origin_(true),
      source_url_(source_url),
      ssl_status_(form_structure.ssl_status),
      callback_(callback),
      account_chooser_model_(this, profile_->GetPrefs(), metric_logger_,
                             dialog_type),
      wallet_client_(profile_->GetRequestContext(), this),
      suggested_email_(this),
      suggested_cc_(this),
      suggested_billing_(this),
      suggested_cc_billing_(this),
      suggested_shipping_(this),
      input_showing_popup_(NULL),
      weak_ptr_factory_(this),
      is_first_run_(!profile_->GetPrefs()->HasPrefPath(
          ::prefs::kAutofillDialogPayWithoutWallet)),
      is_submitting_(false),
      wallet_server_validation_error_(false),
      autocheckout_state_(AUTOCHECKOUT_NOT_STARTED),
      was_ui_latency_logged_(false) {
  // TODO(estade): remove duplicates from |form_structure|?
  DCHECK(!callback_.is_null());
}

AutofillDialogView* AutofillDialogControllerImpl::CreateView() {
  return AutofillDialogView::Create(this);
}

PersonalDataManager* AutofillDialogControllerImpl::GetManager() {
  return PersonalDataManagerFactory::GetForProfile(profile_);
}

wallet::WalletClient* AutofillDialogControllerImpl::GetWalletClient() {
  return &wallet_client_;
}

bool AutofillDialogControllerImpl::IsPayingWithWallet() const {
  return account_chooser_model_.WalletIsSelected() &&
         SignedInState() == SIGNED_IN;
}

bool AutofillDialogControllerImpl::IsFirstRun() const {
  return is_first_run_;
}

void AutofillDialogControllerImpl::OpenTabWithUrl(const GURL& url) {
#if !defined(OS_ANDROID)
  chrome::NavigateParams params(
      chrome::FindBrowserWithWebContents(web_contents()),
      url,
      content::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = NEW_FOREGROUND_TAB;
  chrome::Navigate(&params);
#else
  // TODO(estade): use TabModelList?
#endif
}

void AutofillDialogControllerImpl::DisableWallet() {
  signin_helper_.reset();
  account_chooser_model_.SetHadWalletError();
  GetWalletClient()->CancelRequests();
  wallet_items_.reset();
  full_wallet_.reset();
  SetIsSubmitting(false);
}

void AutofillDialogControllerImpl::SuggestionsUpdated() {
  suggested_email_.Reset();
  suggested_cc_.Reset();
  suggested_billing_.Reset();
  suggested_cc_billing_.Reset();
  suggested_shipping_.Reset();
  HidePopup();

  suggested_shipping_.AddKeyedItem(
      kSameAsBillingKey,
      l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_USE_BILLING_FOR_SHIPPING));

  if (IsPayingWithWallet()) {
    if (!account_chooser_model_.active_wallet_account_name().empty()) {
      suggested_email_.AddKeyedItem(
          base::IntToString(0),
          account_chooser_model_.active_wallet_account_name());
    }

    const std::vector<wallet::Address*>& addresses =
        wallet_items_->addresses();
    for (size_t i = 0; i < addresses.size(); ++i) {
      std::string key = base::IntToString(i);
      suggested_shipping_.AddKeyedItemWithSublabel(
          key,
          addresses[i]->DisplayName(),
          addresses[i]->DisplayNameDetail());

      if (addresses[i]->object_id() ==
              wallet_items_->default_address_id()) {
        suggested_shipping_.SetCheckedItem(key);
      }
    }

    if (!IsSubmitPausedOn(wallet::VERIFY_CVV)) {
      const std::vector<wallet::WalletItems::MaskedInstrument*>& instruments =
          wallet_items_->instruments();
      std::string first_active_instrument_key;
      std::string default_instrument_key;
      for (size_t i = 0; i < instruments.size(); ++i) {
        bool allowed = IsInstrumentAllowed(*instruments[i]);
        gfx::Image icon = instruments[i]->CardIcon();
        if (!allowed && !icon.IsEmpty()) {
          // Create a grayed disabled icon.
          SkBitmap disabled_bitmap = SkBitmapOperations::CreateHSLShiftedBitmap(
              *icon.ToSkBitmap(), kGrayImageShift);
          icon = gfx::Image(
              gfx::ImageSkia::CreateFrom1xBitmap(disabled_bitmap));
        }
        std::string key = base::IntToString(i);
        suggested_cc_billing_.AddKeyedItemWithSublabelAndIcon(
            key,
            instruments[i]->DisplayName(),
            instruments[i]->DisplayNameDetail(),
            icon);
        suggested_cc_billing_.SetEnabled(key, allowed);

        if (allowed) {
          if (first_active_instrument_key.empty())
            first_active_instrument_key = key;
          if (instruments[i]->object_id() ==
              wallet_items_->default_instrument_id()) {
            default_instrument_key = key;
          }
        }
      }

      // TODO(estade): this should have a URL sublabel.
      suggested_cc_billing_.AddKeyedItem(
          kAddNewItemKey,
          l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_ADD_BILLING_DETAILS));
      suggested_cc_billing_.AddKeyedItem(
          kManageItemsKey,
          l10n_util::GetStringUTF16(
              IDS_AUTOFILL_DIALOG_MANAGE_BILLING_DETAILS));

      // Determine which instrument item should be selected.
      if (!default_instrument_key.empty())
        suggested_cc_billing_.SetCheckedItem(default_instrument_key);
      else if (!first_active_instrument_key.empty())
        suggested_cc_billing_.SetCheckedItem(first_active_instrument_key);
      else
        suggested_cc_billing_.SetCheckedItem(kAddNewItemKey);
    }
  } else {
    PersonalDataManager* manager = GetManager();
    const std::vector<CreditCard*>& cards = manager->GetCreditCards();
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    for (size_t i = 0; i < cards.size(); ++i) {
      suggested_cc_.AddKeyedItemWithIcon(
          cards[i]->guid(),
          cards[i]->Label(),
          rb.GetImageNamed(cards[i]->IconResourceId()));
    }

    const std::vector<AutofillProfile*>& profiles = manager->GetProfiles();
    const std::string app_locale = g_browser_process->GetApplicationLocale();
    for (size_t i = 0; i < profiles.size(); ++i) {
      if (!IsCompleteProfile(*profiles[i]))
        continue;

      // Add all email addresses.
      std::vector<string16> values;
      profiles[i]->GetMultiInfo(EMAIL_ADDRESS, app_locale, &values);
      for (size_t j = 0; j < values.size(); ++j) {
        if (!values[j].empty())
          suggested_email_.AddKeyedItem(profiles[i]->guid(), values[j]);
      }

      // Don't add variants for addresses: the email variants are handled above,
      // name is part of credit card and we'll just ignore phone number
      // variants.
      suggested_billing_.AddKeyedItem(profiles[i]->guid(),
                                      profiles[i]->Label());
      suggested_shipping_.AddKeyedItem(profiles[i]->guid(),
                                       profiles[i]->Label());
    }

    suggested_cc_.AddKeyedItem(
        kAddNewItemKey,
        l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_ADD_CREDIT_CARD));
    suggested_cc_.AddKeyedItem(
        kManageItemsKey,
        l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_MANAGE_CREDIT_CARD));
    suggested_billing_.AddKeyedItem(
        kAddNewItemKey,
        l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_ADD_BILLING_ADDRESS));
    suggested_billing_.AddKeyedItem(
        kManageItemsKey,
        l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_MANAGE_BILLING_ADDRESS));
  }

  suggested_email_.AddKeyedItem(
      kAddNewItemKey,
      l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_ADD_EMAIL_ADDRESS));
  if (!IsPayingWithWallet()) {
    suggested_email_.AddKeyedItem(
        kManageItemsKey,
        l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_MANAGE_EMAIL_ADDRESS));
  }

  suggested_shipping_.AddKeyedItem(
      kAddNewItemKey,
      l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_ADD_SHIPPING_ADDRESS));
  suggested_shipping_.AddKeyedItem(
      kManageItemsKey,
      l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_MANAGE_SHIPPING_ADDRESS));

  if (!IsPayingWithWallet()) {
    // When using Autofill, the default option is the first suggestion, if
    // one exists. Otherwise it's the "Use shipping for billing" item.
    const std::string& first_real_suggestion_item_key =
        suggested_shipping_.GetItemKeyAt(1);
    if (IsASuggestionItemKey(first_real_suggestion_item_key))
      suggested_shipping_.SetCheckedItem(first_real_suggestion_item_key);
  }

  if (view_)
    view_->ModelChanged();

  for (size_t section = SECTION_MIN; section <= SECTION_MAX; ++section) {
    PrepareDetailInputsForSection(static_cast<DialogSection>(section));
  }
}

bool AutofillDialogControllerImpl::IsCompleteProfile(
    const AutofillProfile& profile) {
  const std::string app_locale = g_browser_process->GetApplicationLocale();
  for (size_t i = 0; i < requested_shipping_fields_.size(); ++i) {
    AutofillFieldType type = requested_shipping_fields_[i].type;
    if (type != ADDRESS_HOME_LINE2 &&
        profile.GetInfo(type, app_locale).empty()) {
      return false;
    }
  }

  return true;
}

void AutofillDialogControllerImpl::FillOutputForSectionWithComparator(
    DialogSection section,
    const InputFieldComparator& compare) {
  // Email is hidden while using Wallet, special case it.
  if (section == SECTION_EMAIL && IsPayingWithWallet()) {
    AutofillProfile profile;
    profile.SetRawInfo(EMAIL_ADDRESS,
                       account_chooser_model_.active_wallet_account_name());
    FillFormStructureForSection(profile, 0, section, compare);
    return;
  }

  if (!SectionIsActive(section))
    return;

  scoped_ptr<DataModelWrapper> wrapper = CreateWrapper(section);
  if (wrapper) {
    // Only fill in data that is associated with this section.
    const DetailInputs& inputs = RequestedFieldsForSection(section);
    wrapper->FillFormStructure(inputs, compare, &form_structure_);

    // CVC needs special-casing because the CreditCard class doesn't store or
    // handle them. This isn't necessary when filling the combined CC and
    // billing section as CVC comes from |full_wallet_| in this case.
    if (section == SECTION_CC)
      SetCvcResult(view_->GetCvc());
  } else {
    // The user manually input data. If using Autofill, save the info as new or
    // edited data. Always fill local data into |form_structure_|.
    DetailOutputMap output;
    view_->GetUserInput(section, &output);

    if (section == SECTION_CC) {
      CreditCard card;
      card.set_origin(kAutofillDialogOrigin);
      FillFormGroupFromOutputs(output, &card);

      if (ShouldSaveDetailsLocally())
        GetManager()->SaveImportedCreditCard(card);

      FillFormStructureForSection(card, 0, section, compare);

      // Again, CVC needs special-casing. Fill it in directly from |output|.
      SetCvcResult(GetValueForType(output, CREDIT_CARD_VERIFICATION_CODE));
    } else {
      AutofillProfile profile;
      profile.set_origin(kAutofillDialogOrigin);
      FillFormGroupFromOutputs(output, &profile);

      // For billing, the profile name has to come from the CC section.
      if (section == SECTION_BILLING)
        profile.SetRawInfo(NAME_FULL, GetCcName());

      if (ShouldSaveDetailsLocally())
        GetManager()->SaveImportedProfile(profile);

      FillFormStructureForSection(profile, 0, section, compare);
    }
  }
}

void AutofillDialogControllerImpl::FillOutputForSection(DialogSection section) {
  FillOutputForSectionWithComparator(section,
                                     base::Bind(DetailInputMatchesField));
}

void AutofillDialogControllerImpl::FillFormStructureForSection(
    const AutofillDataModel& data_model,
    size_t variant,
    DialogSection section,
    const InputFieldComparator& compare) {
  std::string app_locale = g_browser_process->GetApplicationLocale();
  for (size_t i = 0; i < form_structure_.field_count(); ++i) {
    AutofillField* field = form_structure_.field(i);
    // Only fill in data that is associated with this section.
    const DetailInputs& inputs = RequestedFieldsForSection(section);
    for (size_t j = 0; j < inputs.size(); ++j) {
      if (compare.Run(inputs[j], *field)) {
        data_model.FillFormField(*field, variant, app_locale, field);
        break;
      }
    }
  }
}

void AutofillDialogControllerImpl::SetCvcResult(const string16& cvc) {
  for (size_t i = 0; i < form_structure_.field_count(); ++i) {
    AutofillField* field = form_structure_.field(i);
    if (field->type() == CREDIT_CARD_VERIFICATION_CODE) {
      field->value = cvc;
      break;
    }
  }
}

string16 AutofillDialogControllerImpl::GetCcName() {
  DCHECK(SectionIsActive(SECTION_CC));

  CreditCard card;
  scoped_ptr<DataModelWrapper> wrapper = CreateWrapper(SECTION_CC);
  if (!wrapper) {
    DetailOutputMap output;
    view_->GetUserInput(SECTION_CC, &output);
    FillFormGroupFromOutputs(output, &card);
    wrapper.reset(new AutofillCreditCardWrapper(&card));
  }

  return wrapper->GetInfo(CREDIT_CARD_NAME);
}

SuggestionsMenuModel* AutofillDialogControllerImpl::
    SuggestionsMenuModelForSection(DialogSection section) {
  switch (section) {
    case SECTION_EMAIL:
      return &suggested_email_;
    case SECTION_CC:
      return &suggested_cc_;
    case SECTION_BILLING:
      return &suggested_billing_;
    case SECTION_SHIPPING:
      return &suggested_shipping_;
    case SECTION_CC_BILLING:
      return &suggested_cc_billing_;
  }

  NOTREACHED();
  return NULL;
}

const SuggestionsMenuModel* AutofillDialogControllerImpl::
    SuggestionsMenuModelForSection(DialogSection section) const {
  return const_cast<AutofillDialogControllerImpl*>(this)->
      SuggestionsMenuModelForSection(section);
}

DialogSection AutofillDialogControllerImpl::SectionForSuggestionsMenuModel(
    const SuggestionsMenuModel& model) {
  if (&model == &suggested_email_)
    return SECTION_EMAIL;

  if (&model == &suggested_cc_)
    return SECTION_CC;

  if (&model == &suggested_billing_)
    return SECTION_BILLING;

  if (&model == &suggested_cc_billing_)
    return SECTION_CC_BILLING;

  DCHECK_EQ(&model, &suggested_shipping_);
  return SECTION_SHIPPING;
}

DetailInputs* AutofillDialogControllerImpl::MutableRequestedFieldsForSection(
    DialogSection section) {
  return const_cast<DetailInputs*>(&RequestedFieldsForSection(section));
}

void AutofillDialogControllerImpl::HidePopup() {
  if (popup_controller_)
    popup_controller_->Hide();
  input_showing_popup_ = NULL;
}

void AutofillDialogControllerImpl::LoadRiskFingerprintData() {
  // TODO(dbeam): Add a CHECK or otherwise strong guarantee that the ToS have
  // been accepted prior to calling into this method. Also, ensure that the UI
  // contains a clear indication to the user as to what data will be collected.
  // Until then, this code should not be called. http://crbug.com/173505

  int64 gaia_id = 0;
  bool success =
      base::StringToInt64(wallet_items_->obfuscated_gaia_id(), &gaia_id);
  DCHECK(success);

  gfx::Rect window_bounds =
      GetBaseWindowForWebContents(web_contents())->GetBounds();

  PrefService* user_prefs = profile_->GetPrefs();
  std::string charset = user_prefs->GetString(::prefs::kDefaultCharset);
  std::string accept_languages =
      user_prefs->GetString(::prefs::kAcceptLanguages);
  base::Time install_time = base::Time::FromTimeT(
      g_browser_process->local_state()->GetInt64(::prefs::kInstallDate));

  risk::GetFingerprint(
      gaia_id, window_bounds, *web_contents(), chrome::VersionInfo().Version(),
      charset, accept_languages, install_time, GetDialogType(),
      g_browser_process->GetApplicationLocale(),
      base::Bind(&AutofillDialogControllerImpl::OnDidLoadRiskFingerprintData,
                 weak_ptr_factory_.GetWeakPtr()));
}

void AutofillDialogControllerImpl::OnDidLoadRiskFingerprintData(
    scoped_ptr<risk::Fingerprint> fingerprint) {
  NOTIMPLEMENTED();
}

bool AutofillDialogControllerImpl::IsManuallyEditingSection(
    DialogSection section) const {
  std::map<DialogSection, bool>::const_iterator it =
      section_editing_state_.find(section);
  return (it != section_editing_state_.end() && it->second) ||
         SuggestionsMenuModelForSection(section)->
             GetItemKeyForCheckedItem() == kAddNewItemKey;
}

bool AutofillDialogControllerImpl::IsASuggestionItemKey(
    const std::string& key) {
  return !key.empty() &&
      key != kAddNewItemKey &&
      key != kManageItemsKey &&
      key != kSameAsBillingKey;
}

bool AutofillDialogControllerImpl::IsManuallyEditingAnySection() const {
  for (size_t section = SECTION_MIN; section <= SECTION_MAX; ++section) {
    if (IsManuallyEditingSection(static_cast<DialogSection>(section)))
      return true;
  }
  return false;
}

bool AutofillDialogControllerImpl::InputIsValid(AutofillFieldType type,
                                                const string16& value) const {
  switch (AutofillType::GetEquivalentFieldType(type)) {
    case EMAIL_ADDRESS:
      return IsValidEmailAddress(value);

    case CREDIT_CARD_NUMBER:
      return autofill::IsValidCreditCardNumber(value);
    case CREDIT_CARD_NAME:
      break;
    case CREDIT_CARD_EXP_MONTH:
    case CREDIT_CARD_EXP_4_DIGIT_YEAR:
      break;
    case CREDIT_CARD_VERIFICATION_CODE:
      return autofill::IsValidCreditCardSecurityCode(value);

    case ADDRESS_HOME_LINE1:
      break;
    case ADDRESS_HOME_LINE2:
      return true;  // Line 2 is optional - always valid.
    case ADDRESS_HOME_CITY:
    case ADDRESS_HOME_STATE:
    case ADDRESS_HOME_ZIP:
    case ADDRESS_HOME_COUNTRY:
      break;

    case NAME_FULL:  // Used for shipping.
      break;

    case PHONE_HOME_WHOLE_NUMBER:  // Used in billing section.
      break;

    default:
      NOTREACHED();  // Trying to validate unknown field.
      break;
  }

  return !value.empty();
}

bool AutofillDialogControllerImpl::AllSectionsAreValid() const {
  for (size_t section = SECTION_MIN; section <= SECTION_MAX; ++section) {
    if (!SectionIsValid(static_cast<DialogSection>(section)))
      return false;
  }
  return true;
}

bool AutofillDialogControllerImpl::SectionIsValid(
    DialogSection section) const {
  if (!IsManuallyEditingSection(section))
    return true;

  DetailOutputMap detail_outputs;
  view_->GetUserInput(section, &detail_outputs);
  return InputsAreValid(detail_outputs, VALIDATE_EDIT).empty();
}

bool AutofillDialogControllerImpl::ShouldUseBillingForShipping() {
  return suggested_shipping_.GetItemKeyForCheckedItem() == kSameAsBillingKey;
}

bool AutofillDialogControllerImpl::ShouldSaveDetailsLocally() {
  // It's possible that the user checked [X] Save details locally before
  // switching payment methods, so only ask the view whether to save details
  // locally if that checkbox is showing (currently if not paying with wallet).
  // Also, if the user isn't editing any sections, there's no data to save
  // locally.
  return ShouldOfferToSaveInChrome() && view_->SaveDetailsLocally();
}

void AutofillDialogControllerImpl::SetIsSubmitting(bool submitting) {
  is_submitting_ = submitting;

  if (view_) {
    view_->UpdateButtonStrip();
    view_->UpdateNotificationArea();
  }
}

void AutofillDialogControllerImpl::SubmitWithWallet() {
  // TODO(dbeam): disallow interacting with the dialog while submitting.
  // http://crbug.com/230932

  active_instrument_id_.clear();
  active_address_id_.clear();
  full_wallet_.reset();

  content::BrowserThread::PostTask(
     content::BrowserThread::IO, FROM_HERE,
     base::Bind(&UserDidOptIntoLocationServices));

  GetWalletClient()->AcceptLegalDocuments(
      wallet_items_->legal_documents(),
      wallet_items_->google_transaction_id(),
      source_url_);

  SuggestionsMenuModel* billing =
      SuggestionsMenuModelForSection(SECTION_CC_BILLING);
  int instrument_index = -1;
  base::StringToInt(billing->GetItemKeyForCheckedItem(), &instrument_index);

  if (!IsManuallyEditingSection(SECTION_CC_BILLING)) {
    active_instrument_id_ =
        wallet_items_->instruments()[instrument_index]->object_id();
    DCHECK(!active_instrument_id_.empty());
  }

  SuggestionsMenuModel* shipping =
      SuggestionsMenuModelForSection(SECTION_SHIPPING);
  int address_index = -1;
  base::StringToInt(shipping->GetItemKeyForCheckedItem(), &address_index);

  if (!IsManuallyEditingSection(SECTION_SHIPPING) &&
      shipping->GetItemKeyForCheckedItem() != kSameAsBillingKey) {
    active_address_id_ =
        wallet_items_->addresses()[address_index]->object_id();
    DCHECK(!active_address_id_.empty());
  }

  if (!active_instrument_id_.empty() && !active_address_id_.empty()) {
    GetFullWallet();
    return;
  }

  scoped_ptr<wallet::Instrument> inputted_instrument =
      CreateTransientInstrument();
  scoped_ptr<wallet::WalletClient::UpdateInstrumentRequest> update_request =
      CreateUpdateInstrumentRequest(
          inputted_instrument.get(),
          !section_editing_state_[SECTION_CC_BILLING] ? std::string() :
              wallet_items_->instruments()[instrument_index]->object_id());

  scoped_ptr<wallet::Address> inputted_address;
  if (active_address_id_.empty()) {
    if (ShouldUseBillingForShipping()) {
      inputted_address.reset(new wallet::Address(inputted_instrument ?
          inputted_instrument->address() :
          wallet_items_->instruments()[instrument_index]->address()));
      DCHECK(inputted_address->object_id().empty());
    } else {
      inputted_address = CreateTransientAddress();
      if (section_editing_state_[SECTION_SHIPPING]) {
        inputted_address->set_object_id(
            wallet_items_->addresses()[address_index]->object_id());
        DCHECK(!inputted_address->object_id().empty());
      }
    }
  }

  // If instrument and address aren't based off of any existing data, save both.
  if (inputted_instrument && inputted_address && !update_request &&
      inputted_address->object_id().empty()) {
    GetWalletClient()->SaveInstrumentAndAddress(
        *inputted_instrument,
        *inputted_address,
        wallet_items_->obfuscated_gaia_id(),
        source_url_);
    return;
  }

  if (inputted_instrument) {
    if (update_request) {
      scoped_ptr<wallet::Address> billing_address(
          new wallet::Address(inputted_instrument->address()));
      GetWalletClient()->UpdateInstrument(*update_request,
                                          billing_address.Pass());
    } else {
      GetWalletClient()->SaveInstrument(*inputted_instrument,
                                        wallet_items_->obfuscated_gaia_id(),
                                        source_url_);
    }
  }

  if (inputted_address) {
    if (!inputted_address->object_id().empty())
      GetWalletClient()->UpdateAddress(*inputted_address, source_url_);
    else
      GetWalletClient()->SaveAddress(*inputted_address, source_url_);
  }
}

scoped_ptr<wallet::Instrument> AutofillDialogControllerImpl::
    CreateTransientInstrument() {
  if (!active_instrument_id_.empty())
    return scoped_ptr<wallet::Instrument>();

  DetailOutputMap output;
  view_->GetUserInput(SECTION_CC_BILLING, &output);

  CreditCard card;
  AutofillProfile profile;
  string16 cvc;
  GetBillingInfoFromOutputs(output, &card, &cvc, &profile);

  return scoped_ptr<wallet::Instrument>(
      new wallet::Instrument(card, cvc, profile));
}

scoped_ptr<wallet::WalletClient::UpdateInstrumentRequest>
    AutofillDialogControllerImpl::CreateUpdateInstrumentRequest(
        const wallet::Instrument* instrument,
        const std::string& instrument_id) {
  if (!instrument || instrument_id.empty())
    return scoped_ptr<wallet::WalletClient::UpdateInstrumentRequest>();

  scoped_ptr<wallet::WalletClient::UpdateInstrumentRequest> update_request(
      new wallet::WalletClient::UpdateInstrumentRequest(
          instrument_id, source_url_));
  update_request->expiration_month = instrument->expiration_month();
  update_request->expiration_year = instrument->expiration_year();
  update_request->card_verification_number =
      UTF16ToUTF8(instrument->card_verification_number());
  update_request->obfuscated_gaia_id = wallet_items_->obfuscated_gaia_id();
  return update_request.Pass();
}

scoped_ptr<wallet::Address>AutofillDialogControllerImpl::
    CreateTransientAddress() {
  // If not using billing for shipping, just scrape the view.
  DetailOutputMap output;
  view_->GetUserInput(SECTION_SHIPPING, &output);

  AutofillProfile profile;
  FillFormGroupFromOutputs(output, &profile);

  return scoped_ptr<wallet::Address>(new wallet::Address(profile));
}

void AutofillDialogControllerImpl::GetFullWallet() {
  DCHECK(is_submitting_);
  DCHECK(IsPayingWithWallet());
  DCHECK(wallet_items_);
  DCHECK(!active_instrument_id_.empty());
  DCHECK(!active_address_id_.empty());

  std::vector<wallet::WalletClient::RiskCapability> capabilities;
  capabilities.push_back(wallet::WalletClient::VERIFY_CVC);

  GetWalletClient()->GetFullWallet(wallet::WalletClient::FullWalletRequest(
      active_instrument_id_,
      active_address_id_,
      source_url_,
      wallet::Cart(base::IntToString(kCartMax), kCartCurrency),
      wallet_items_->google_transaction_id(),
      capabilities));
}

void AutofillDialogControllerImpl::HandleSaveOrUpdateRequiredActions(
    const std::vector<wallet::RequiredAction>& required_actions) {
  DCHECK(!required_actions.empty());

  for (std::vector<wallet::RequiredAction>::const_iterator iter =
           required_actions.begin();
       iter != required_actions.end(); ++iter) {
    if (*iter == wallet::INVALID_FORM_FIELD) {
      wallet_server_validation_error_ = true;
    } else {
      // TODO(dbeam): handle this more gracefully.
      DisableWallet();
    }
  }

  SetIsSubmitting(false);
}

void AutofillDialogControllerImpl::FinishSubmit() {
  FillOutputForSection(SECTION_EMAIL);
  FillOutputForSection(SECTION_CC);
  FillOutputForSection(SECTION_BILLING);
  FillOutputForSection(SECTION_CC_BILLING);

  if (ShouldUseBillingForShipping()) {
    FillOutputForSectionWithComparator(
        SECTION_BILLING,
        base::Bind(DetailInputMatchesShippingField));
    FillOutputForSectionWithComparator(
        SECTION_CC,
        base::Bind(DetailInputMatchesShippingField));
    FillOutputForSectionWithComparator(
        SECTION_CC_BILLING,
        base::Bind(DetailInputMatchesShippingField));
  } else {
    FillOutputForSection(SECTION_SHIPPING);
  }

  callback_.Run(&form_structure_, !wallet_items_ ? std::string() :
      wallet_items_->google_transaction_id());
  callback_ = base::Callback<void(const FormStructure*, const std::string&)>();

  LogOnFinishSubmitMetrics();

  // On a successful submit, if the user manually selected "pay without wallet",
  // stop trying to pay with Wallet on future runs of the dialog.
  bool manually_selected_pay_without_wallet =
      !account_chooser_model_.WalletIsSelected() &&
      !account_chooser_model_.had_wallet_error();
  profile_->GetPrefs()->SetBoolean(::prefs::kAutofillDialogPayWithoutWallet,
                                   manually_selected_pay_without_wallet);

  switch (GetDialogType()) {
    case DIALOG_TYPE_AUTOCHECKOUT:
      // Stop observing PersonalDataManager to avoid the dialog redrawing while
      // in an Autocheckout flow.
      GetManager()->RemoveObserver(this);
      autocheckout_started_timestamp_ = base::Time::Now();
      DCHECK_EQ(AUTOCHECKOUT_NOT_STARTED, autocheckout_state_);
      autocheckout_state_ = AUTOCHECKOUT_IN_PROGRESS;
      view_->UpdateButtonStrip();
      view_->UpdateDetailArea();
      view_->UpdateNotificationArea();
      break;

    case DIALOG_TYPE_REQUEST_AUTOCOMPLETE:
      // This may delete us.
      Hide();
      break;
  }
}

void AutofillDialogControllerImpl::LogOnFinishSubmitMetrics() {
  GetMetricLogger().LogDialogUiDuration(
      base::Time::Now() - dialog_shown_timestamp_,
      GetDialogType(),
      AutofillMetrics::DIALOG_ACCEPTED);

  GetMetricLogger().LogDialogUiEvent(
      GetDialogType(), AutofillMetrics::DIALOG_UI_ACCEPTED);

  AutofillMetrics::DialogDismissalState dismissal_state;
  if (!IsManuallyEditingAnySection())
    dismissal_state = AutofillMetrics::DIALOG_ACCEPTED_EXISTING_DATA;
  else if (IsPayingWithWallet())
    dismissal_state = AutofillMetrics::DIALOG_ACCEPTED_SAVE_TO_WALLET;
  else if (ShouldSaveDetailsLocally())
    dismissal_state = AutofillMetrics::DIALOG_ACCEPTED_SAVE_TO_AUTOFILL;
  else
    dismissal_state = AutofillMetrics::DIALOG_ACCEPTED_NO_SAVE;

  GetMetricLogger().LogDialogDismissalState(GetDialogType(), dismissal_state);
}

void AutofillDialogControllerImpl::LogOnCancelMetrics() {
  GetMetricLogger().LogDialogUiEvent(
      GetDialogType(), AutofillMetrics::DIALOG_UI_CANCELED);

  AutofillMetrics::DialogDismissalState dismissal_state;
  if (!IsManuallyEditingAnySection())
    dismissal_state = AutofillMetrics::DIALOG_CANCELED_NO_EDITS;
  else if (AllSectionsAreValid())
    dismissal_state = AutofillMetrics::DIALOG_CANCELED_NO_INVALID_FIELDS;
  else
    dismissal_state = AutofillMetrics::DIALOG_CANCELED_WITH_INVALID_FIELDS;

  GetMetricLogger().LogDialogDismissalState(GetDialogType(), dismissal_state);

  GetMetricLogger().LogDialogUiDuration(
      base::Time::Now() - dialog_shown_timestamp_,
      GetDialogType(),
      AutofillMetrics::DIALOG_CANCELED);
}

void AutofillDialogControllerImpl::LogSuggestionItemSelectedMetric(
    const SuggestionsMenuModel& model) {
  DialogSection section = SectionForSuggestionsMenuModel(model);

  AutofillMetrics::DialogUiEvent dialog_ui_event;
  if (model.GetItemKeyForCheckedItem() == kAddNewItemKey) {
    // Selected to add a new item.
    dialog_ui_event = DialogSectionToUiItemAddedEvent(section);
  } else if (IsASuggestionItemKey(model.GetItemKeyForCheckedItem())) {
    // Selected an existing item.
    dialog_ui_event = DialogSectionToUiSelectionChangedEvent(section);
  } else {
    // TODO(estade): add logging for "Manage items" or "Use billing for
    // shipping"?
    return;
  }

  GetMetricLogger().LogDialogUiEvent(GetDialogType(), dialog_ui_event);
}

void AutofillDialogControllerImpl::LogDialogLatencyToShow() {
  if (was_ui_latency_logged_)
    return;

  GetMetricLogger().LogDialogLatencyToShow(
      GetDialogType(),
      base::Time::Now() - dialog_shown_timestamp_);
  was_ui_latency_logged_ = true;
}

AutofillMetrics::DialogInitialUserStateMetric
    AutofillDialogControllerImpl::GetInitialUserState() const {
  // Consider a user to be an Autofill user if the user has any credit cards
  // or addresses saved. Check that the item count is greater than 2 because
  // an "empty" menu still has the "add new" menu item and "manage" menu item.
  const bool has_autofill_profiles =
      suggested_cc_.GetItemCount() > 2 ||
      suggested_billing_.GetItemCount() > 2;

  if (SignedInState() != SIGNED_IN) {
    // Not signed in.
    return has_autofill_profiles ?
        AutofillMetrics::DIALOG_USER_NOT_SIGNED_IN_HAS_AUTOFILL :
        AutofillMetrics::DIALOG_USER_NOT_SIGNED_IN_NO_AUTOFILL;
  }

  // Signed in.
  if (wallet_items_->instruments().empty()) {
    // No Wallet items.
    return has_autofill_profiles ?
        AutofillMetrics::DIALOG_USER_SIGNED_IN_NO_WALLET_HAS_AUTOFILL :
        AutofillMetrics::DIALOG_USER_SIGNED_IN_NO_WALLET_NO_AUTOFILL;
  }

  // Has Wallet items.
  return has_autofill_profiles ?
      AutofillMetrics::DIALOG_USER_SIGNED_IN_HAS_WALLET_HAS_AUTOFILL :
      AutofillMetrics::DIALOG_USER_SIGNED_IN_HAS_WALLET_NO_AUTOFILL;
}

}  // namespace autofill
