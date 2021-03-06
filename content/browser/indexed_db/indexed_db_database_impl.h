// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_INDEXED_DB_INDEXED_DB_DATABASE_IMPL_H_
#define CONTENT_BROWSER_INDEXED_DB_INDEXED_DB_DATABASE_IMPL_H_

#include <list>
#include <map>
#include <vector>

#include "base/basictypes.h"
#include "content/browser/indexed_db/indexed_db_callbacks_wrapper.h"
#include "content/browser/indexed_db/indexed_db_metadata.h"
#include "content/browser/indexed_db/indexed_db_transaction_coordinator.h"
#include "content/browser/indexed_db/list_set.h"

namespace content {

class IndexedDBBackingStore;
class IndexedDBDatabase;
class IndexedDBFactoryImpl;
class IndexedDBTransaction;

class CONTENT_EXPORT IndexedDBDatabaseImpl
    : NON_EXPORTED_BASE(public IndexedDBDatabase) {
 public:
  static scoped_refptr<IndexedDBDatabaseImpl> Create(
      const string16& name,
      IndexedDBBackingStore* database,
      IndexedDBFactoryImpl* factory,
      const string16& unique_identifier);
  scoped_refptr<IndexedDBBackingStore> BackingStore() const;

  static const int64 kInvalidId = 0;
  int64 id() const { return metadata_.id; }
  void AddObjectStore(const IndexedDBObjectStoreMetadata& metadata,
                      int64 new_max_object_store_id);
  void RemoveObjectStore(int64 object_store_id);
  void AddIndex(int64 object_store_id,
                const IndexedDBIndexMetadata& metadata,
                int64 new_max_index_id);
  void RemoveIndex(int64 object_store_id, int64 index_id);

  void OpenConnection(
      scoped_refptr<IndexedDBCallbacksWrapper> callbacks,
      scoped_refptr<IndexedDBDatabaseCallbacksWrapper> database_callbacks,
      int64 transaction_id,
      int64 version);
  void DeleteDatabase(scoped_refptr<IndexedDBCallbacksWrapper> callbacks);
  const IndexedDBDatabaseMetadata& metadata() const { return metadata_; }

  // IndexedDBDatabase
  virtual void CreateObjectStore(int64 transaction_id,
                                 int64 object_store_id,
                                 const string16& name,
                                 const IndexedDBKeyPath& key_path,
                                 bool auto_increment) OVERRIDE;
  virtual void DeleteObjectStore(int64 transaction_id, int64 object_store_id)
      OVERRIDE;
  virtual void CreateTransaction(
      int64 transaction_id,
      scoped_refptr<IndexedDBDatabaseCallbacksWrapper> callbacks,
      const std::vector<int64>& object_store_ids,
      uint16 mode) OVERRIDE;
  virtual void Close(scoped_refptr<IndexedDBDatabaseCallbacksWrapper> callbacks)
      OVERRIDE;

  virtual void Commit(int64 transaction_id) OVERRIDE;
  virtual void Abort(int64 transaction_id) OVERRIDE;
  virtual void Abort(int64 transaction_id, const IndexedDBDatabaseError& error)
      OVERRIDE;

  virtual void CreateIndex(int64 transaction_id,
                           int64 object_store_id,
                           int64 index_id,
                           const string16& name,
                           const IndexedDBKeyPath& key_path,
                           bool unique,
                           bool multi_entry) OVERRIDE;
  virtual void DeleteIndex(int64 transaction_id,
                           int64 object_store_id,
                           int64 index_id) OVERRIDE;

  IndexedDBTransactionCoordinator& transaction_coordinator() {
    return transaction_coordinator_;
  }

  void TransactionStarted(IndexedDBTransaction* transaction);
  void TransactionFinished(IndexedDBTransaction* transaction);
  void TransactionFinishedAndCompleteFired(IndexedDBTransaction* transaction);
  void TransactionFinishedAndAbortFired(IndexedDBTransaction* transaction);

  virtual void Get(int64 transaction_id,
                   int64 object_store_id,
                   int64 index_id,
                   scoped_ptr<IndexedDBKeyRange> key_range,
                   bool key_only,
                   scoped_refptr<IndexedDBCallbacksWrapper> callbacks) OVERRIDE;
  virtual void Put(int64 transaction_id,
                   int64 object_store_id,
                   std::vector<char>* value,
                   scoped_ptr<IndexedDBKey> key,
                   PutMode mode,
                   scoped_refptr<IndexedDBCallbacksWrapper> callbacks,
                   const std::vector<int64>& index_ids,
                   const std::vector<IndexKeys>& index_keys) OVERRIDE;
  virtual void SetIndexKeys(int64 transaction_id,
                            int64 object_store_id,
                            scoped_ptr<IndexedDBKey> primary_key,
                            const std::vector<int64>& index_ids,
                            const std::vector<IndexKeys>& index_keys) OVERRIDE;
  virtual void SetIndexesReady(int64 transaction_id,
                               int64 object_store_id,
                               const std::vector<int64>& index_ids) OVERRIDE;
  virtual void OpenCursor(int64 transaction_id,
                          int64 object_store_id,
                          int64 index_id,
                          scoped_ptr<IndexedDBKeyRange> key_range,
                          indexed_db::CursorDirection,
                          bool key_only,
                          TaskType task_type,
                          scoped_refptr<IndexedDBCallbacksWrapper> callbacks)
      OVERRIDE;
  virtual void Count(int64 transaction_id,
                     int64 object_store_id,
                     int64 index_id,
                     scoped_ptr<IndexedDBKeyRange> key_range,
                     scoped_refptr<IndexedDBCallbacksWrapper> callbacks)
      OVERRIDE;
  virtual void DeleteRange(int64 transaction_id,
                           int64 object_store_id,
                           scoped_ptr<IndexedDBKeyRange> key_range,
                           scoped_refptr<IndexedDBCallbacksWrapper> callbacks)
      OVERRIDE;
  virtual void Clear(int64 transaction_id,
                     int64 object_store_id,
                     scoped_refptr<IndexedDBCallbacksWrapper> callbacks)
      OVERRIDE;

 private:
  IndexedDBDatabaseImpl(const string16& name,
                        IndexedDBBackingStore* database,
                        IndexedDBFactoryImpl* factory,
                        const string16& unique_identifier);
  virtual ~IndexedDBDatabaseImpl();

  bool IsOpenConnectionBlocked() const;
  bool OpenInternal();
  void RunVersionChangeTransaction(
      scoped_refptr<IndexedDBCallbacksWrapper> callbacks,
      scoped_refptr<IndexedDBDatabaseCallbacksWrapper> database_callbacks,
      int64 transaction_id,
      int64 requested_version);
  void RunVersionChangeTransactionFinal(
      scoped_refptr<IndexedDBCallbacksWrapper> callbacks,
      scoped_refptr<IndexedDBDatabaseCallbacksWrapper> database_callbacks,
      int64 transaction_id,
      int64 requested_version);
  size_t ConnectionCount() const;
  void ProcessPendingCalls();

  bool IsDeleteDatabaseBlocked() const;
  void DeleteDatabaseFinal(scoped_refptr<IndexedDBCallbacksWrapper> callbacks);

  class VersionChangeOperation;

  // When a "versionchange" transaction aborts, these restore the back-end
  // object hierarchy.
  class VersionChangeAbortOperation;

  scoped_refptr<IndexedDBBackingStore> backing_store_;
  IndexedDBDatabaseMetadata metadata_;

  string16 identifier_;
  // This might not need to be a scoped_refptr since the factory's lifetime is
  // that of the page group, but it's better to be conservitive than sorry.
  scoped_refptr<IndexedDBFactoryImpl> factory_;

  IndexedDBTransactionCoordinator transaction_coordinator_;
  IndexedDBTransaction* running_version_change_transaction_;

  typedef std::map<int64, IndexedDBTransaction*> TransactionMap;
  TransactionMap transactions_;

  class PendingOpenCall;
  typedef std::list<PendingOpenCall*> PendingOpenCallList;
  PendingOpenCallList pending_open_calls_;
  scoped_ptr<PendingOpenCall> pending_run_version_change_transaction_call_;
  scoped_ptr<PendingOpenCall> pending_second_half_open_;

  class PendingDeleteCall;
  typedef std::list<PendingDeleteCall*> PendingDeleteCallList;
  PendingDeleteCallList pending_delete_calls_;

  typedef list_set<scoped_refptr<IndexedDBDatabaseCallbacksWrapper> >
      DatabaseCallbacksSet;
  DatabaseCallbacksSet database_callbacks_set_;

  bool closing_connection_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_INDEXED_DB_INDEXED_DB_DATABASE_IMPL_H_
