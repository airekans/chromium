// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/scheduler/scheduler.h"

#include "base/auto_reset.h"
#include "base/debug/trace_event.h"
#include "base/logging.h"

namespace cc {

Scheduler::Scheduler(SchedulerClient* client,
                     scoped_ptr<FrameRateController> frame_rate_controller,
                     const SchedulerSettings& scheduler_settings)
    : settings_(scheduler_settings),
      client_(client),
      frame_rate_controller_(frame_rate_controller.Pass()),
      state_machine_(scheduler_settings),
      inside_process_scheduled_actions_(false) {
  DCHECK(client_);
  frame_rate_controller_->SetClient(this);
  DCHECK(!state_machine_.BeginFrameNeededByImplThread());
}

Scheduler::~Scheduler() { frame_rate_controller_->SetActive(false); }

void Scheduler::SetCanStart() {
  state_machine_.SetCanStart();
  ProcessScheduledActions();
}

void Scheduler::SetVisible(bool visible) {
  state_machine_.SetVisible(visible);
  ProcessScheduledActions();
}

void Scheduler::SetCanDraw(bool can_draw) {
  state_machine_.SetCanDraw(can_draw);
  ProcessScheduledActions();
}

void Scheduler::SetHasPendingTree(bool has_pending_tree) {
  state_machine_.SetHasPendingTree(has_pending_tree);
  ProcessScheduledActions();
}

void Scheduler::SetNeedsCommit() {
  state_machine_.SetNeedsCommit();
  ProcessScheduledActions();
}

void Scheduler::SetNeedsForcedCommit() {
  state_machine_.SetNeedsCommit();
  state_machine_.SetNeedsForcedCommit();
  ProcessScheduledActions();
}

void Scheduler::SetNeedsRedraw() {
  state_machine_.SetNeedsRedraw();
  ProcessScheduledActions();
}

void Scheduler::DidSwapUseIncompleteTile() {
  state_machine_.DidSwapUseIncompleteTile();
  ProcessScheduledActions();
}

void Scheduler::SetNeedsForcedRedraw() {
  state_machine_.SetNeedsForcedRedraw();
  ProcessScheduledActions();
}

void Scheduler::SetMainThreadNeedsLayerTextures() {
  state_machine_.SetMainThreadNeedsLayerTextures();
  ProcessScheduledActions();
}

void Scheduler::FinishCommit() {
  TRACE_EVENT0("cc", "Scheduler::FinishCommit");
  state_machine_.FinishCommit();
  ProcessScheduledActions();
}

void Scheduler::BeginFrameAbortedByMainThread() {
  TRACE_EVENT0("cc", "Scheduler::BeginFrameAbortedByMainThread");
  state_machine_.BeginFrameAbortedByMainThread();
  ProcessScheduledActions();
}

void Scheduler::SetMaxFramesPending(int max_frames_pending) {
  frame_rate_controller_->SetMaxFramesPending(max_frames_pending);
}

int Scheduler::MaxFramesPending() const {
  return frame_rate_controller_->MaxFramesPending();
}

int Scheduler::NumFramesPendingForTesting() const {
  return frame_rate_controller_->NumFramesPendingForTesting();
}

void Scheduler::SetSwapBuffersCompleteSupported(bool supported) {
  frame_rate_controller_->SetSwapBuffersCompleteSupported(supported);
}

void Scheduler::DidSwapBuffersComplete() {
  TRACE_EVENT0("cc", "Scheduler::DidSwapBuffersComplete");
  frame_rate_controller_->DidSwapBuffersComplete();
}

void Scheduler::DidLoseOutputSurface() {
  TRACE_EVENT0("cc", "Scheduler::DidLoseOutputSurface");
  state_machine_.DidLoseOutputSurface();
  ProcessScheduledActions();
}

void Scheduler::DidCreateAndInitializeOutputSurface() {
  TRACE_EVENT0("cc", "Scheduler::DidCreateAndInitializeOutputSurface");
  frame_rate_controller_->DidAbortAllPendingFrames();
  state_machine_.DidCreateAndInitializeOutputSurface();
  ProcessScheduledActions();
}

void Scheduler::SetTimebaseAndInterval(base::TimeTicks timebase,
                                       base::TimeDelta interval) {
  frame_rate_controller_->SetTimebaseAndInterval(timebase, interval);
}

base::TimeTicks Scheduler::AnticipatedDrawTime() {
  return frame_rate_controller_->NextTickTime();
}

base::TimeTicks Scheduler::LastBeginFrameOnImplThreadTime() {
  return frame_rate_controller_->LastTickTime();
}

void Scheduler::BeginFrame(bool throttled) {
  TRACE_EVENT1("cc", "Scheduler::BeginFrame", "throttled", throttled);
  if (!throttled)
    state_machine_.DidEnterBeginFrame();
  ProcessScheduledActions();
  if (!throttled)
    state_machine_.DidLeaveBeginFrame();
}

void Scheduler::ProcessScheduledActions() {
  // We do not allow ProcessScheduledActions to be recursive.
  // The top-level call will iteratively execute the next action for us anyway.
  if (inside_process_scheduled_actions_)
    return;

  base::AutoReset<bool> mark_inside(&inside_process_scheduled_actions_, true);

  SchedulerStateMachine::Action action = state_machine_.NextAction();
  while (action != SchedulerStateMachine::ACTION_NONE) {
    state_machine_.UpdateState(action);
    TRACE_EVENT1(
        "cc", "Scheduler::ProcessScheduledActions()", "action", action);

    switch (action) {
      case SchedulerStateMachine::ACTION_NONE:
        break;
      case SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD:
        client_->ScheduledActionSendBeginFrameToMainThread();
        break;
      case SchedulerStateMachine::ACTION_COMMIT:
        client_->ScheduledActionCommit();
        break;
      case SchedulerStateMachine::ACTION_CHECK_FOR_COMPLETED_TILE_UPLOADS:
        client_->ScheduledActionCheckForCompletedTileUploads();
        break;
      case SchedulerStateMachine::ACTION_ACTIVATE_PENDING_TREE_IF_NEEDED:
        client_->ScheduledActionActivatePendingTreeIfNeeded();
        break;
      case SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE: {
        ScheduledActionDrawAndSwapResult result =
            client_->ScheduledActionDrawAndSwapIfPossible();
        state_machine_.DidDrawIfPossibleCompleted(result.did_draw);
        if (result.did_swap)
          frame_rate_controller_->DidSwapBuffers();
        break;
      }
      case SchedulerStateMachine::ACTION_DRAW_FORCED: {
        ScheduledActionDrawAndSwapResult result =
            client_->ScheduledActionDrawAndSwapForced();
        if (result.did_swap)
          frame_rate_controller_->DidSwapBuffers();
        break;
      }
      case SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION:
        client_->ScheduledActionBeginOutputSurfaceCreation();
        break;
      case SchedulerStateMachine::ACTION_ACQUIRE_LAYER_TEXTURES_FOR_MAIN_THREAD:
        client_->ScheduledActionAcquireLayerTexturesForMainThread();
        break;
    }
    action = state_machine_.NextAction();
  }

  // Activate or deactivate the frame rate controller.
  frame_rate_controller_->SetActive(
      state_machine_.BeginFrameNeededByImplThread());
  client_->DidAnticipatedDrawTimeChange(frame_rate_controller_->NextTickTime());
}

bool Scheduler::WillDrawIfNeeded() const {
  return !state_machine_.DrawSuspendedUntilCommit();
}

}  // namespace cc
