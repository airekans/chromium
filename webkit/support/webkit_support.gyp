# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'includes': [
    '../common/user_agent/webkit_user_agent.gypi',
  ],
  'conditions': [
    # TODO(stuartmorgan): All dependencies from code built on iOS to
    # webkit/ should be removed, at which point this condition can be
    # removed.
    ['OS != "ios"', {
      'includes': [
        '../../build/win_precompile.gypi',
        '../appcache/webkit_appcache.gypi',
        '../base/webkit_base.gypi',
        '../glue/webkit_glue.gypi',
        '../media/webkit_media.gypi',
        '../plugins/webkit_plugins.gypi',
        '../quota/webkit_quota.gypi',
        '../storage/webkit_storage.gypi',
        'webkit_support.gypi',
      ],
    }],
  ],
}
