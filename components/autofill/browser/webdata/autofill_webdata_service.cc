// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/browser/webdata/autofill_webdata_service.h"

#include "base/logging.h"
#include "base/stl_util.h"
#include "components/autofill/browser/autofill_country.h"
#include "components/autofill/browser/autofill_profile.h"
#include "components/autofill/browser/credit_card.h"
#include "components/autofill/browser/webdata/autofill_change.h"
#include "components/autofill/browser/webdata/autofill_entry.h"
#include "components/autofill/browser/webdata/autofill_table.h"
#include "components/autofill/browser/webdata/autofill_webdata_backend_impl.h"
#include "components/autofill/browser/webdata/autofill_webdata_service_observer.h"
#include "components/autofill/common/form_field_data.h"
#include "components/webdata/common/web_data_service_backend.h"
#include "components/webdata/common/web_database_service.h"

using base::Bind;
using base::Time;
using content::BrowserThread;

namespace autofill {

AutofillWebDataService::AutofillWebDataService(
    scoped_refptr<WebDatabaseService> wdbs,
    const ProfileErrorCallback& callback)
    : WebDataServiceBase(wdbs, callback),
      weak_ptr_factory_(this),
      autofill_backend_(NULL) {

  base::Closure on_changed_callback = Bind(
      &AutofillWebDataService::NotifyAutofillMultipleChangedOnUIThread,
      weak_ptr_factory_.GetWeakPtr());

  autofill_backend_ = new AutofillWebDataBackendImpl(
      wdbs_->GetBackend(),
      on_changed_callback);
}

AutofillWebDataService::AutofillWebDataService()
    : WebDataServiceBase(NULL,
                         WebDataServiceBase::ProfileErrorCallback()),
      weak_ptr_factory_(this),
      autofill_backend_(new AutofillWebDataBackendImpl(NULL, base::Closure())) {
}

void AutofillWebDataService::ShutdownOnUIThread() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  BrowserThread::PostTask(BrowserThread::DB, FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::ResetUserData,
           autofill_backend_));
  WebDataServiceBase::ShutdownOnUIThread();
}

void AutofillWebDataService::AddFormFields(
    const std::vector<FormFieldData>& fields) {
  wdbs_->ScheduleDBTask(FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::AddFormElements,
           autofill_backend_, fields));
}

WebDataServiceBase::Handle AutofillWebDataService::GetFormValuesForElementName(
    const base::string16& name, const base::string16& prefix, int limit,
    WebDataServiceConsumer* consumer) {
  return wdbs_->ScheduleDBTaskWithResult(FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::GetFormValuesForElementName,
           autofill_backend_, name, prefix, limit), consumer);
}

WebDataServiceBase::Handle AutofillWebDataService::HasFormElements(
    WebDataServiceConsumer* consumer) {
  return wdbs_->ScheduleDBTaskWithResult(FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::HasFormElements, autofill_backend_),
      consumer);
}

void AutofillWebDataService::RemoveFormElementsAddedBetween(
    const Time& delete_begin, const Time& delete_end) {
  wdbs_->ScheduleDBTask(FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::RemoveFormElementsAddedBetween,
           autofill_backend_, delete_begin, delete_end));
}

void AutofillWebDataService::RemoveFormValueForElementName(
    const base::string16& name, const base::string16& value) {
  wdbs_->ScheduleDBTask(FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::RemoveFormValueForElementName,
           autofill_backend_, name, value));
}

void AutofillWebDataService::AddAutofillProfile(
    const AutofillProfile& profile) {
  wdbs_->ScheduleDBTask(FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::AddAutofillProfile,
           autofill_backend_, profile));
}

void AutofillWebDataService::UpdateAutofillProfile(
    const AutofillProfile& profile) {
  wdbs_->ScheduleDBTask(FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::UpdateAutofillProfile,
           autofill_backend_, profile));
}

void AutofillWebDataService::RemoveAutofillProfile(
    const std::string& guid) {
  wdbs_->ScheduleDBTask(FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::RemoveAutofillProfile,
           autofill_backend_, guid));
}

WebDataServiceBase::Handle AutofillWebDataService::GetAutofillProfiles(
    WebDataServiceConsumer* consumer) {
  return wdbs_->ScheduleDBTaskWithResult(FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::GetAutofillProfiles, autofill_backend_),
      consumer);
}

void AutofillWebDataService::AddCreditCard(const CreditCard& credit_card) {
  wdbs_->ScheduleDBTask(
      FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::AddCreditCard,
           autofill_backend_, credit_card));
}

void AutofillWebDataService::UpdateCreditCard(
    const CreditCard& credit_card) {
  wdbs_->ScheduleDBTask(
      FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::UpdateCreditCard,
           autofill_backend_, credit_card));
}

void AutofillWebDataService::RemoveCreditCard(const std::string& guid) {
  wdbs_->ScheduleDBTask(
      FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::RemoveCreditCard,
           autofill_backend_, guid));
}

WebDataServiceBase::Handle AutofillWebDataService::GetCreditCards(
    WebDataServiceConsumer* consumer) {
  return wdbs_->ScheduleDBTaskWithResult(FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::GetCreditCards, autofill_backend_),
      consumer);
}

void AutofillWebDataService::RemoveAutofillDataModifiedBetween(
    const Time& delete_begin,
    const Time& delete_end) {
  wdbs_->ScheduleDBTask(
      FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::RemoveAutofillDataModifiedBetween,
           autofill_backend_, delete_begin, delete_end));
}

void AutofillWebDataService::RemoveOriginURLsModifiedBetween(
    const Time& delete_begin, const Time& delete_end) {
  wdbs_->ScheduleDBTask(
      FROM_HERE,
      Bind(&AutofillWebDataBackendImpl::RemoveOriginURLsModifiedBetween,
           autofill_backend_, delete_begin, delete_end));
}

void AutofillWebDataService::AddObserver(
    AutofillWebDataServiceObserverOnDBThread* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  if (autofill_backend_.get())
    autofill_backend_->AddObserver(observer);
}

void AutofillWebDataService::RemoveObserver(
    AutofillWebDataServiceObserverOnDBThread* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  if (autofill_backend_.get())
    autofill_backend_->RemoveObserver(observer);
}

void AutofillWebDataService::AddObserver(
    AutofillWebDataServiceObserverOnUIThread* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ui_observer_list_.AddObserver(observer);
}

void AutofillWebDataService::RemoveObserver(
    AutofillWebDataServiceObserverOnUIThread* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ui_observer_list_.RemoveObserver(observer);
}

base::SupportsUserData* AutofillWebDataService::GetDBUserData() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  return autofill_backend_->GetDBUserData();
}

void AutofillWebDataService::GetAutofillBackend(
    const base::Callback<void(AutofillWebDataBackend*)>& callback) {
  BrowserThread::PostTask(BrowserThread::DB,
                          FROM_HERE,
                          base::Bind(callback, autofill_backend_));
}

AutofillWebDataService::~AutofillWebDataService() {
}

void AutofillWebDataService::NotifyAutofillMultipleChangedOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  FOR_EACH_OBSERVER(AutofillWebDataServiceObserverOnUIThread,
                    ui_observer_list_,
                    AutofillMultipleChanged());
}

}  // namespace autofill
