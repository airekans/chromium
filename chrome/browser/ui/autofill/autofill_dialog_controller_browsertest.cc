// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/command_line.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/autofill/autofill_dialog_controller_impl.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view.h"
#include "chrome/browser/ui/autofill/data_model_wrapper.h"
#include "chrome/browser/ui/autofill/testable_autofill_dialog_view.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/autofill/browser/autofill_common_test.h"
#include "components/autofill/browser/autofill_metrics.h"
#include "components/autofill/browser/test_personal_data_manager.h"
#include "components/autofill/browser/validation.h"
#include "components/autofill/browser/wallet/wallet_test_util.h"
#include "components/autofill/common/autofill_switches.h"
#include "components/autofill/common/form_data.h"
#include "components/autofill/common/form_field_data.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill {

namespace {

void MockCallback(const FormStructure*, const std::string&) {}

class MockAutofillMetrics : public AutofillMetrics {
 public:
  MockAutofillMetrics()
      : dialog_type_(static_cast<DialogType>(-1)),
        dialog_dismissal_action_(
            static_cast<AutofillMetrics::DialogDismissalAction>(-1)),
        autocheckout_status_(
            static_cast<AutofillMetrics::AutocheckoutCompletionStatus>(-1)) {}
  virtual ~MockAutofillMetrics() {}

  // AutofillMetrics:
  virtual void LogAutocheckoutDuration(
      const base::TimeDelta& duration,
      AutocheckoutCompletionStatus status) const OVERRIDE {
    // Ignore constness for testing.
    MockAutofillMetrics* mutable_this = const_cast<MockAutofillMetrics*>(this);
    mutable_this->autocheckout_status_ = status;
  }

  virtual void LogDialogUiDuration(
      const base::TimeDelta& duration,
      DialogType dialog_type,
      DialogDismissalAction dismissal_action) const OVERRIDE {
    // Ignore constness for testing.
    MockAutofillMetrics* mutable_this = const_cast<MockAutofillMetrics*>(this);
    mutable_this->dialog_type_ = dialog_type;
    mutable_this->dialog_dismissal_action_ = dismissal_action;
  }

  DialogType dialog_type() const { return dialog_type_; }
  AutofillMetrics::DialogDismissalAction dialog_dismissal_action() const {
    return dialog_dismissal_action_;
  }

  AutofillMetrics::AutocheckoutCompletionStatus autocheckout_status() const {
    return autocheckout_status_;
  }

 private:
  DialogType dialog_type_;
  AutofillMetrics::DialogDismissalAction dialog_dismissal_action_;
  AutofillMetrics::AutocheckoutCompletionStatus autocheckout_status_;

  DISALLOW_COPY_AND_ASSIGN(MockAutofillMetrics);
};

class TestAutofillDialogController : public AutofillDialogControllerImpl {
 public:
  TestAutofillDialogController(content::WebContents* contents,
                               const FormData& form_data,
                               const AutofillMetrics& metric_logger,
                               scoped_refptr<content::MessageLoopRunner> runner,
                               const DialogType dialog_type)
      : AutofillDialogControllerImpl(contents,
                                     form_data,
                                     GURL(),
                                     dialog_type,
                                     base::Bind(&MockCallback)),
        metric_logger_(metric_logger),
        message_loop_runner_(runner) {}

  virtual ~TestAutofillDialogController() {}

  virtual void ViewClosed() OVERRIDE {
    message_loop_runner_->Quit();
    AutofillDialogControllerImpl::ViewClosed();
  }

  virtual string16 InputValidityMessage(
      AutofillFieldType type,
      const string16& value) const OVERRIDE {
    return string16();
  }

  virtual ValidityData InputsAreValid(
      const DetailOutputMap& inputs,
      ValidationType validation_type) const OVERRIDE {
    return ValidityData();
  }

  // Saving to Chrome is tested in AutofillDialogController unit tests.
  // TODO(estade): test that the view defaults to saving to Chrome.
  virtual bool ShouldOfferToSaveInChrome() const OVERRIDE {
    return false;
  }

  // Increase visibility for testing.
  using AutofillDialogControllerImpl::view;
  using AutofillDialogControllerImpl::input_showing_popup;

  virtual std::vector<DialogNotification> CurrentNotifications() OVERRIDE {
    return notifications_;
  }

  void set_notifications(const std::vector<DialogNotification>& notifications) {
    notifications_ = notifications;
  }

  TestPersonalDataManager* GetTestingManager() {
    return &test_manager_;
  }

  using AutofillDialogControllerImpl::DisableWallet;
  using AutofillDialogControllerImpl::IsEditingExistingData;

 protected:
  virtual PersonalDataManager* GetManager() OVERRIDE {
    return &test_manager_;
  }

 private:
  // To specify our own metric logger.
  virtual const AutofillMetrics& GetMetricLogger() const OVERRIDE {
    return metric_logger_;
  }

  const AutofillMetrics& metric_logger_;
  TestPersonalDataManager test_manager_;
  scoped_refptr<content::MessageLoopRunner> message_loop_runner_;

  // A list of notifications to show in the notification area of the dialog.
  // This is used to control what |CurrentNotifications()| returns for testing.
  std::vector<DialogNotification> notifications_;

  DISALLOW_COPY_AND_ASSIGN(TestAutofillDialogController);
};

}  // namespace

class AutofillDialogControllerTest : public InProcessBrowserTest {
 public:
  AutofillDialogControllerTest() {}
  virtual ~AutofillDialogControllerTest() {}

  void InitializeControllerOfType(DialogType dialog_type) {
    FormData form;
    form.name = ASCIIToUTF16("TestForm");
    form.method = ASCIIToUTF16("POST");
    form.origin = GURL("http://example.com/form.html");
    form.action = GURL("http://example.com/submit.html");
    form.user_submitted = true;

    FormFieldData field;
    field.autocomplete_attribute = "email";
    form.fields.push_back(field);

    message_loop_runner_ = new content::MessageLoopRunner;
    controller_ = new TestAutofillDialogController(
        GetActiveWebContents(),
        form,
        metric_logger_,
        message_loop_runner_,
        dialog_type);
    controller_->Show();
  }

  content::WebContents* GetActiveWebContents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  const MockAutofillMetrics& metric_logger() { return metric_logger_; }
  TestAutofillDialogController* controller() { return controller_; }

  void RunMessageLoop() {
    message_loop_runner_->Run();
  }

 private:
  MockAutofillMetrics metric_logger_;
  TestAutofillDialogController* controller_;  // Weak reference.
  scoped_refptr<content::MessageLoopRunner> message_loop_runner_;
  DISALLOW_COPY_AND_ASSIGN(AutofillDialogControllerTest);
};

// TODO(isherman): Enable these tests on other platforms once the UI is
// implemented on those platforms.
#if defined(TOOLKIT_VIEWS)
// Submit the form data.
IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest, Submit) {
  InitializeControllerOfType(DIALOG_TYPE_REQUEST_AUTOCOMPLETE);
  controller()->view()->GetTestableView()->SubmitForTesting();

  RunMessageLoop();

  EXPECT_EQ(AutofillMetrics::DIALOG_ACCEPTED,
            metric_logger().dialog_dismissal_action());
  EXPECT_EQ(DIALOG_TYPE_REQUEST_AUTOCOMPLETE, metric_logger().dialog_type());
}

// Cancel out of the dialog.
IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest, Cancel) {
  InitializeControllerOfType(DIALOG_TYPE_REQUEST_AUTOCOMPLETE);
  controller()->view()->GetTestableView()->CancelForTesting();

  RunMessageLoop();

  EXPECT_EQ(AutofillMetrics::DIALOG_CANCELED,
            metric_logger().dialog_dismissal_action());
  EXPECT_EQ(DIALOG_TYPE_REQUEST_AUTOCOMPLETE, metric_logger().dialog_type());
}

// Take some other action that dismisses the dialog.
IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest, Hide) {
  InitializeControllerOfType(DIALOG_TYPE_REQUEST_AUTOCOMPLETE);
  controller()->Hide();

  RunMessageLoop();

  EXPECT_EQ(AutofillMetrics::DIALOG_CANCELED,
            metric_logger().dialog_dismissal_action());
  EXPECT_EQ(DIALOG_TYPE_REQUEST_AUTOCOMPLETE, metric_logger().dialog_type());
}

// Test Autocheckout success metrics.
IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest, AutocheckoutSuccess) {
  InitializeControllerOfType(DIALOG_TYPE_AUTOCHECKOUT);
  controller()->view()->GetTestableView()->SubmitForTesting();

  EXPECT_EQ(AutofillMetrics::DIALOG_ACCEPTED,
            metric_logger().dialog_dismissal_action());
  EXPECT_EQ(DIALOG_TYPE_AUTOCHECKOUT, metric_logger().dialog_type());

  controller()->OnAutocheckoutSuccess();
  controller()->view()->GetTestableView()->CancelForTesting();
  RunMessageLoop();

  EXPECT_EQ(AutofillMetrics::AUTOCHECKOUT_SUCCEEDED,
            metric_logger().autocheckout_status());
}

// Test Autocheckout failure metric.
IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest, AutocheckoutError) {
  InitializeControllerOfType(DIALOG_TYPE_AUTOCHECKOUT);
  controller()->view()->GetTestableView()->SubmitForTesting();

  EXPECT_EQ(AutofillMetrics::DIALOG_ACCEPTED,
            metric_logger().dialog_dismissal_action());
  EXPECT_EQ(DIALOG_TYPE_AUTOCHECKOUT, metric_logger().dialog_type());

  controller()->OnAutocheckoutError();
  controller()->view()->GetTestableView()->CancelForTesting();
  RunMessageLoop();

  EXPECT_EQ(AutofillMetrics::AUTOCHECKOUT_FAILED,
            metric_logger().autocheckout_status());
}

IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest, FillInputFromAutofill) {
  InitializeControllerOfType(DIALOG_TYPE_REQUEST_AUTOCOMPLETE);
  controller()->DisableWallet();

  AutofillProfile full_profile(test::GetFullProfile());
  controller()->GetTestingManager()->AddTestingProfile(&full_profile);

  const DetailInputs& inputs =
      controller()->RequestedFieldsForSection(SECTION_SHIPPING);
  const DetailInput& triggering_input = inputs[0];
  string16 value = full_profile.GetRawInfo(triggering_input.type);
  TestableAutofillDialogView* view = controller()->view()->GetTestableView();
  view->SetTextContentsOfInput(triggering_input,
                               value.substr(0, value.size() / 2));
  view->ActivateInput(triggering_input);

  ASSERT_EQ(&triggering_input, controller()->input_showing_popup());
  controller()->DidAcceptSuggestion(string16(), 0);

  // All inputs should be filled.
  AutofillProfileWrapper wrapper(&full_profile, 0);
  for (size_t i = 0; i < inputs.size(); ++i) {
    EXPECT_EQ(wrapper.GetInfo(inputs[i].type),
              view->GetTextContentsOfInput(inputs[i]));
  }

  // Now simulate some user edits and try again.
  std::vector<string16> expectations;
  for (size_t i = 0; i < inputs.size(); ++i) {
    string16 users_input = i % 2 == 0 ? string16() : ASCIIToUTF16("dummy");
    view->SetTextContentsOfInput(inputs[i], users_input);
    // Empty inputs should be filled, others should be left alone.
    string16 expectation =
        &inputs[i] == &triggering_input || users_input.empty() ?
        wrapper.GetInfo(inputs[i].type) :
        users_input;
    expectations.push_back(expectation);
  }

  view->SetTextContentsOfInput(triggering_input,
                               value.substr(0, value.size() / 2));
  view->ActivateInput(triggering_input);
  ASSERT_EQ(&triggering_input, controller()->input_showing_popup());
  controller()->DidAcceptSuggestion(string16(), 0);

  for (size_t i = 0; i < inputs.size(); ++i) {
    EXPECT_EQ(expectations[i], view->GetTextContentsOfInput(inputs[i]));
  }
}

// Test that the Autocheckout progress bar is showing after submitting the
// dialog for controller with type DIALOG_TYPE_AUTOCHECKOUT.
IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest,
                       AutocheckoutShowsProgressBar) {
  InitializeControllerOfType(DIALOG_TYPE_AUTOCHECKOUT);
  EXPECT_TRUE(controller()->ShouldShowDetailArea());
  EXPECT_FALSE(controller()->ShouldShowProgressBar());

  controller()->view()->GetTestableView()->SubmitForTesting();
  EXPECT_FALSE(controller()->ShouldShowDetailArea());
  EXPECT_TRUE(controller()->ShouldShowProgressBar());
}

// Test that the Autocheckout progress bar is not showing after submitting the
// dialog for controller with type DIALOG_TYPE_REQUEST_AUTOCOMPLETE.
IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest,
                       RequestAutocompleteDoesntShowProgressBar) {
  InitializeControllerOfType(DIALOG_TYPE_REQUEST_AUTOCOMPLETE);
  EXPECT_TRUE(controller()->ShouldShowDetailArea());
  EXPECT_FALSE(controller()->ShouldShowProgressBar());

  controller()->view()->GetTestableView()->SubmitForTesting();
  EXPECT_TRUE(controller()->ShouldShowDetailArea());
  EXPECT_FALSE(controller()->ShouldShowProgressBar());
}

// Tests that changing the value of a CC expiration date combobox works as
// expected when Autofill is used to fill text inputs.
IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest, FillComboboxFromAutofill) {
  InitializeControllerOfType(DIALOG_TYPE_REQUEST_AUTOCOMPLETE);
  controller()->DisableWallet();

  CreditCard card1;
  test::SetCreditCardInfo(&card1, "JJ Smith", "4111111111111111", "12", "2018");
  controller()->GetTestingManager()->AddTestingCreditCard(&card1);
  CreditCard card2;
  test::SetCreditCardInfo(&card2, "B Bird", "3111111111111111", "11", "2017");
  controller()->GetTestingManager()->AddTestingCreditCard(&card2);
  AutofillProfile full_profile(test::GetFullProfile());
  controller()->GetTestingManager()->AddTestingProfile(&full_profile);

  const DetailInputs& inputs =
      controller()->RequestedFieldsForSection(SECTION_CC);
  const DetailInput& triggering_input = inputs[0];
  string16 value = card1.GetRawInfo(triggering_input.type);
  TestableAutofillDialogView* view = controller()->view()->GetTestableView();
  view->SetTextContentsOfInput(triggering_input,
                               value.substr(0, value.size() / 2));
  view->ActivateInput(triggering_input);

  ASSERT_EQ(&triggering_input, controller()->input_showing_popup());
  controller()->DidAcceptSuggestion(string16(), 0);

  // All inputs should be filled.
  AutofillCreditCardWrapper wrapper1(&card1);
  for (size_t i = 0; i < inputs.size(); ++i) {
    EXPECT_EQ(wrapper1.GetInfo(inputs[i].type),
              view->GetTextContentsOfInput(inputs[i]));
  }

  // Try again with different data. Only expiration date and the triggering
  // input should be overwritten.
  value = card2.GetRawInfo(triggering_input.type);
  view->SetTextContentsOfInput(triggering_input,
                               value.substr(0, value.size() / 2));
  view->ActivateInput(triggering_input);
  ASSERT_EQ(&triggering_input, controller()->input_showing_popup());
  controller()->DidAcceptSuggestion(string16(), 0);

  AutofillCreditCardWrapper wrapper2(&card2);
  for (size_t i = 0; i < inputs.size(); ++i) {
    const DetailInput& input = inputs[i];
    if (&input == &triggering_input ||
        input.type == CREDIT_CARD_EXP_MONTH ||
        input.type == CREDIT_CARD_EXP_4_DIGIT_YEAR) {
      EXPECT_EQ(wrapper2.GetInfo(input.type),
                view->GetTextContentsOfInput(input));
    } else if (input.type == CREDIT_CARD_VERIFICATION_CODE) {
      EXPECT_TRUE(view->GetTextContentsOfInput(input).empty());
    } else {
      EXPECT_EQ(wrapper1.GetInfo(input.type),
                view->GetTextContentsOfInput(input));
    }
  }

  // Now fill from a profile. It should not overwrite any CC info.
  const DetailInputs& billing_inputs =
      controller()->RequestedFieldsForSection(SECTION_BILLING);
  const DetailInput& billing_triggering_input = billing_inputs[0];
  value = full_profile.GetRawInfo(triggering_input.type);
  view->SetTextContentsOfInput(billing_triggering_input,
                               value.substr(0, value.size() / 2));
  view->ActivateInput(billing_triggering_input);

  ASSERT_EQ(&billing_triggering_input, controller()->input_showing_popup());
  controller()->DidAcceptSuggestion(string16(), 0);

  for (size_t i = 0; i < inputs.size(); ++i) {
    const DetailInput& input = inputs[i];
    if (&input == &triggering_input ||
        input.type == CREDIT_CARD_EXP_MONTH ||
        input.type == CREDIT_CARD_EXP_4_DIGIT_YEAR) {
      EXPECT_EQ(wrapper2.GetInfo(input.type),
                view->GetTextContentsOfInput(input));
    } else if (input.type == CREDIT_CARD_VERIFICATION_CODE) {
      EXPECT_TRUE(view->GetTextContentsOfInput(input).empty());
    } else {
      EXPECT_EQ(wrapper1.GetInfo(input.type),
                view->GetTextContentsOfInput(input));
    }
  }
}

// Tests that credit card number is disabled while editing a Wallet instrument.
IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest, WalletCreditCardDisabled) {
  InitializeControllerOfType(DIALOG_TYPE_REQUEST_AUTOCOMPLETE);
  controller()->OnUserNameFetchSuccess("user@example.com");

  scoped_ptr<wallet::WalletItems> wallet_items = wallet::GetTestWalletItems();
  wallet_items->AddInstrument(wallet::GetTestMaskedInstrument());
  controller()->OnDidGetWalletItems(wallet_items.Pass());

  // Click "Edit" in the billing section (while using Wallet).
  controller()->EditClickedForSection(SECTION_CC_BILLING);

  const DetailInputs& edit_inputs =
      controller()->RequestedFieldsForSection(SECTION_CC_BILLING);
  size_t i;
  for (i = 0; i < edit_inputs.size(); ++i) {
    if (edit_inputs[i].type == CREDIT_CARD_NUMBER) {
      EXPECT_FALSE(edit_inputs[i].editable);
      break;
    }
  }
  ASSERT_LT(i, edit_inputs.size());

  // Select "Add new billing info..." while using Wallet.
  ui::MenuModel* model = controller()->MenuModelForSection(SECTION_CC_BILLING);
  model->ActivatedAt(model->GetItemCount() - 2);

  const DetailInputs& add_inputs =
      controller()->RequestedFieldsForSection(SECTION_CC_BILLING);
  for (i = 0; i < add_inputs.size(); ++i) {
    if (add_inputs[i].type == CREDIT_CARD_NUMBER) {
      EXPECT_TRUE(add_inputs[i].editable);
      break;
    }
  }
  ASSERT_LT(i, add_inputs.size());
}

// Ensure that expired cards trigger invalid suggestions.
IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest, ExpiredCard) {
  InitializeControllerOfType(DIALOG_TYPE_REQUEST_AUTOCOMPLETE);
  controller()->DisableWallet();

  CreditCard verified_card(test::GetCreditCard());
  verified_card.set_origin("Chrome settings");
  ASSERT_TRUE(verified_card.IsVerified());
  controller()->GetTestingManager()->AddTestingCreditCard(&verified_card);

  CreditCard expired_card(test::GetCreditCard());
  expired_card.set_origin("Chrome settings");
  expired_card.SetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR, ASCIIToUTF16("2007"));
  ASSERT_TRUE(expired_card.IsVerified());
  ASSERT_FALSE(
      autofill::IsValidCreditCardExpirationDate(
          expired_card.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR),
          expired_card.GetRawInfo(CREDIT_CARD_EXP_MONTH),
          base::Time::Now()));
  controller()->GetTestingManager()->AddTestingCreditCard(&expired_card);

  ui::MenuModel* model = controller()->MenuModelForSection(SECTION_CC);
  ASSERT_EQ(4, model->GetItemCount());

  ASSERT_TRUE(model->IsItemCheckedAt(0));
  EXPECT_FALSE(controller()->IsEditingExistingData(SECTION_CC));

  model->ActivatedAt(1);
  ASSERT_TRUE(model->IsItemCheckedAt(1));
  EXPECT_TRUE(controller()->IsEditingExistingData(SECTION_CC));
}

// Notifications with long message text should not make the dialog bigger.
IN_PROC_BROWSER_TEST_F(AutofillDialogControllerTest, LongNotifications) {
  InitializeControllerOfType(DIALOG_TYPE_REQUEST_AUTOCOMPLETE);

  const gfx::Size no_notification_size =
      controller()->view()->GetTestableView()->GetSize();
  ASSERT_GT(no_notification_size.width(), 0);

  std::vector<DialogNotification> notifications;
  notifications.push_back(
      DialogNotification(DialogNotification::DEVELOPER_WARNING, ASCIIToUTF16(
          "Lorem ipsum dolor sit amet, consectetur adipisicing elit, sed do "
          "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim "
          "ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut "
          "aliquip ex ea commodo consequat. Duis aute irure dolor in "
          "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
          "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
          "culpa qui officia deserunt mollit anim id est laborum.")));
  controller()->set_notifications(notifications);
  controller()->view()->UpdateNotificationArea();

  EXPECT_EQ(no_notification_size.width(),
            controller()->view()->GetTestableView()->GetSize().width());
}
#endif  // defined(TOOLKIT_VIEWS)

}  // namespace autofill
