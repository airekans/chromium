# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'webkit_fileapi_sources': [
      '../fileapi/syncable/file_change.cc',
      '../fileapi/syncable/file_change.h',
      '../fileapi/syncable/local_file_change_tracker.cc',
      '../fileapi/syncable/local_file_change_tracker.h',
      '../fileapi/syncable/local_file_sync_context.cc',
      '../fileapi/syncable/local_file_sync_context.h',
      '../fileapi/syncable/local_file_sync_status.cc',
      '../fileapi/syncable/local_file_sync_status.h',
      '../fileapi/syncable/local_origin_change_observer.h',
      '../fileapi/syncable/sync_callbacks.h',
      '../fileapi/syncable/sync_file_metadata.cc',
      '../fileapi/syncable/sync_file_metadata.h',
      '../fileapi/syncable/sync_file_status.h',
      '../fileapi/syncable/sync_file_type.h',
      '../fileapi/syncable/sync_action.h',
      '../fileapi/syncable/sync_direction.h',
      '../fileapi/syncable/sync_status_code.cc',
      '../fileapi/syncable/sync_status_code.h',
      '../fileapi/syncable/syncable_file_operation_runner.cc',
      '../fileapi/syncable/syncable_file_operation_runner.h',
      '../fileapi/syncable/syncable_file_system_operation.cc',
      '../fileapi/syncable/syncable_file_system_operation.h',
      '../fileapi/syncable/syncable_file_system_util.cc',
      '../fileapi/syncable/syncable_file_system_util.h',
    ],
    'webkit_fileapi_chromeos_sources': [
      '../chromeos/fileapi/async_file_stream.h',
      '../chromeos/fileapi/cros_mount_point_provider.cc',
      '../chromeos/fileapi/cros_mount_point_provider.h',
      '../chromeos/fileapi/file_access_permissions.cc',
      '../chromeos/fileapi/file_access_permissions.h',
      '../chromeos/fileapi/file_util_async.h',
      '../chromeos/fileapi/remote_file_system_operation.cc',
      '../chromeos/fileapi/remote_file_system_operation.h',
      '../chromeos/fileapi/remote_file_stream_writer.cc',
      '../chromeos/fileapi/remote_file_stream_writer.h',
    ],
  },
}
