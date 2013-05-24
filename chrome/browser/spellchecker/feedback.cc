// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/spellchecker/feedback.h"

#include <algorithm>
#include <iterator>

namespace spellcheck {

Feedback::Feedback() {
}

Feedback::~Feedback() {
}

Misspelling* Feedback::GetMisspelling(uint32 hash) {
  HashMisspellingMap::iterator it = misspellings_.find(hash);
  if (it == misspellings_.end())
    return NULL;
  return &it->second;
}

void Feedback::FinalizeRemovedMisspellings(
    int renderer_process_id,
    const std::vector<uint32>& remaining_markers) {
  RendererHashesMap::iterator i = hashes_.find(renderer_process_id);
  if (i == hashes_.end() || i->second.empty())
    return;
  HashCollection remaining_set(remaining_markers.begin(),
                               remaining_markers.end());
  std::vector<uint32> removed_markers;
  std::set_difference(i->second.begin(),
                      i->second.end(),
                      remaining_set.begin(),
                      remaining_set.end(),
                      std::back_inserter(removed_markers));
  for (std::vector<uint32>::const_iterator j = removed_markers.begin();
       j != removed_markers.end();
       ++j) {
    HashMisspellingMap::iterator k = misspellings_.find(*j);
    if (k != misspellings_.end() && !k->second.action.IsFinal())
      k->second.action.Finalize();
  }
}

bool Feedback::RendererHasMisspellings(int renderer_process_id) const {
  RendererHashesMap::const_iterator it = hashes_.find(renderer_process_id);
  return it != hashes_.end() && !it->second.empty();
}

std::vector<Misspelling> Feedback::GetMisspellingsInRenderer(
    int renderer_process_id) const {
  std::vector<Misspelling> result;
  RendererHashesMap::const_iterator i = hashes_.find(renderer_process_id);
  if (i == hashes_.end() || i->second.empty())
    return result;
  for (HashCollection::const_iterator j = i->second.begin();
       j != i->second.end();
       ++j) {
    HashMisspellingMap::const_iterator k = misspellings_.find(*j);
    if (k != misspellings_.end())
      result.push_back(k->second);
  }
  return result;
}

void Feedback::EraseFinalizedMisspellings(int renderer_process_id) {
  RendererHashesMap::iterator i = hashes_.find(renderer_process_id);
  if (i == hashes_.end() || i->second.empty())
    return;
  HashCollection pending;
  for (HashCollection::const_iterator j = i->second.begin();
       j != i->second.end();
       ++j) {
    HashMisspellingMap::iterator k = misspellings_.find(*j);
    if (k != misspellings_.end()) {
      if (k->second.action.IsFinal())
        misspellings_.erase(k);
      else
        pending.insert(*j);
    }
  }
  i->second.swap(pending);
}

bool Feedback::HasMisspelling(uint32 hash) const {
  return !!misspellings_.count(hash);
}

void Feedback::AddMisspelling(int renderer_process_id,
                              const Misspelling& misspelling) {
  misspellings_[misspelling.hash] = misspelling;
  hashes_[renderer_process_id].insert(misspelling.hash);
}

bool Feedback::Empty() const {
  return misspellings_.empty();
}

std::vector<int> Feedback::GetRendersWithMisspellings() const {
  std::vector<int> result;
  for (RendererHashesMap::const_iterator it = hashes_.begin();
       it != hashes_.end();
       ++it) {
    if (!it->second.empty())
      result.push_back(it->first);
  }
  return result;
}

void Feedback::FinalizeAllMisspellings() {
  for (HashMisspellingMap::iterator it = misspellings_.begin();
       it != misspellings_.end();
       ++it) {
    if (!it->second.action.IsFinal())
      it->second.action.Finalize();
  }
}

std::vector<Misspelling> Feedback::GetAllMisspellings() const {
  std::vector<Misspelling> result;
  for (HashMisspellingMap::const_iterator it = misspellings_.begin();
       it != misspellings_.end();
       ++it) {
    result.push_back(it->second);
  }
  return result;
}

void Feedback::Clear() {
  misspellings_.clear();
  hashes_.clear();
}

}  // namespace spellcheck
