// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/in_process_renderer/in_process_view_renderer.h"

#include <android/bitmap.h>

#include "android_webview/public/browser/draw_gl.h"
#include "android_webview/public/browser/draw_sw.h"
#include "base/android/jni_android.h"
#include "base/debug/trace_event.h"
#include "base/logging.h"
#include "content/public/browser/android/content_view_core.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/renderer/android/synchronous_compositor.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkDevice.h"
#include "third_party/skia/include/core/SkGraphics.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "ui/gfx/size_conversions.h"
#include "ui/gfx/transform.h"
#include "ui/gfx/vector2d_f.h"
#include "ui/gl/gl_bindings.h"

// TODO(leandrogracia): Borrowed from gl2ext.h. Cannot be included due to
// conflicts with  gl_bindings.h and the EGL library methods
// (eglGetCurrentContext).
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

#ifndef GL_TEXTURE_BINDING_EXTERNAL_OES
#define GL_TEXTURE_BINDING_EXTERNAL_OES 0x8D67
#endif

using base::android::AttachCurrentThread;
using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;
using content::Compositor;
using content::ContentViewCore;

namespace android_webview {

namespace {

class GLStateRestore {
 public:
  GLStateRestore() {
#if !defined(NDEBUG)
    {
      GLint vertex_array_buffer_binding;
      glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vertex_array_buffer_binding);
      DCHECK_EQ(0, vertex_array_buffer_binding);

      GLint index_array_buffer_binding;
      glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,
                    &index_array_buffer_binding);
      DCHECK_EQ(0, index_array_buffer_binding);
    }
#endif  // !defined(NDEBUG)
    glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES,
                  &texture_external_oes_binding_);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment_);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment_);

    for (size_t i = 0; i < ARRAYSIZE_UNSAFE(vertex_attrib_); ++i) {
      glGetVertexAttribiv(
          i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &vertex_attrib_[i].enabled);
      glGetVertexAttribiv(
          i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &vertex_attrib_[i].size);
      glGetVertexAttribiv(
          i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &vertex_attrib_[i].type);
      glGetVertexAttribiv(
          i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &vertex_attrib_[i].normalized);
      glGetVertexAttribiv(
          i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &vertex_attrib_[i].stride);
      glGetVertexAttribPointerv(
          i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &vertex_attrib_[i].pointer);
    }

    glGetBooleanv(GL_DEPTH_TEST, &depth_test_);
    glGetBooleanv(GL_CULL_FACE, &cull_face_);
    glGetBooleanv(GL_COLOR_WRITEMASK, color_mask_);
    glGetBooleanv(GL_BLEND, &blend_enabled_);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb_);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha_);
    glGetIntegerv(GL_BLEND_DST_RGB, &blend_dest_rgb_);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dest_alpha_);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture_);
    glGetIntegerv(GL_VIEWPORT, viewport_);
    glGetBooleanv(GL_SCISSOR_TEST, &scissor_test_);
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box_);
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_);
  }

  ~GLStateRestore() {
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture_external_oes_binding_);
    glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment_);

    for (size_t i = 0; i < ARRAYSIZE_UNSAFE(vertex_attrib_); ++i) {
      glVertexAttribPointer(i,
                            vertex_attrib_[i].size,
                            vertex_attrib_[i].type,
                            vertex_attrib_[i].normalized,
                            vertex_attrib_[i].stride,
                            vertex_attrib_[i].pointer);

      if (vertex_attrib_[i].enabled) {
        glEnableVertexAttribArray(i);
      } else {
        glDisableVertexAttribArray(i);
      }
    }

    if (depth_test_) {
      glEnable(GL_DEPTH_TEST);
    } else {
      glDisable(GL_DEPTH_TEST);
    }

    if (cull_face_) {
      glEnable(GL_CULL_FACE);
    } else {
      glDisable(GL_CULL_FACE);
    }

    glColorMask(color_mask_[0], color_mask_[1], color_mask_[2], color_mask_[3]);

    if (blend_enabled_) {
      glEnable(GL_BLEND);
    } else {
      glDisable(GL_BLEND);
    }

    glBlendFuncSeparate(
        blend_src_rgb_, blend_dest_rgb_, blend_src_alpha_, blend_dest_alpha_);
    glActiveTexture(active_texture_);

    glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);

    if (scissor_test_) {
      glEnable(GL_SCISSOR_TEST);
    } else {
      glDisable(GL_SCISSOR_TEST);
    }

    glScissor(
        scissor_box_[0], scissor_box_[1], scissor_box_[2], scissor_box_[3]);

    glUseProgram(current_program_);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  }

 private:
  GLint texture_external_oes_binding_;
  GLint pack_alignment_;
  GLint unpack_alignment_;

  struct {
    GLint enabled;
    GLint size;
    GLint type;
    GLint normalized;
    GLint stride;
    GLvoid* pointer;
  } vertex_attrib_[3];

  GLboolean depth_test_;
  GLboolean cull_face_;
  GLboolean color_mask_[4];
  GLboolean blend_enabled_;
  GLint blend_src_rgb_;
  GLint blend_src_alpha_;
  GLint blend_dest_rgb_;
  GLint blend_dest_alpha_;
  GLint active_texture_;
  GLint viewport_[4];
  GLboolean scissor_test_;
  GLint scissor_box_[4];
  GLint current_program_;
};

const void* kUserDataKey = &kUserDataKey;

class UserData : public content::WebContents::Data {
 public:
  UserData(InProcessViewRenderer* ptr) : instance_(ptr) {}
  virtual ~UserData() {
    instance_->WebContentsGone();
  }

  static InProcessViewRenderer* GetInstance(content::WebContents* contents) {
    if (!contents)
      return NULL;
    UserData* data = reinterpret_cast<UserData*>(
        contents->GetUserData(kUserDataKey));
    return data ? data->instance_ : NULL;
  }

 private:
  InProcessViewRenderer* instance_;
};

typedef base::Callback<bool(SkCanvas*)> RenderMethod;

bool RasterizeIntoBitmap(JNIEnv* env,
                         const JavaRef<jobject>& jbitmap,
                         int scroll_x,
                         int scroll_y,
                         const RenderMethod& renderer) {
  DCHECK(jbitmap.obj());

  AndroidBitmapInfo bitmap_info;
  if (AndroidBitmap_getInfo(env, jbitmap.obj(), &bitmap_info) < 0) {
    LOG(ERROR) << "Error getting java bitmap info.";
    return false;
  }

  void* pixels = NULL;
  if (AndroidBitmap_lockPixels(env, jbitmap.obj(), &pixels) < 0) {
    LOG(ERROR) << "Error locking java bitmap pixels.";
    return false;
  }

  bool succeeded;
  {
    SkBitmap bitmap;
    bitmap.setConfig(SkBitmap::kARGB_8888_Config,
                     bitmap_info.width,
                     bitmap_info.height,
                     bitmap_info.stride);
    bitmap.setPixels(pixels);

    SkDevice device(bitmap);
    SkCanvas canvas(&device);
    canvas.translate(-scroll_x, -scroll_y);
    succeeded = renderer.Run(&canvas);
  }

  if (AndroidBitmap_unlockPixels(env, jbitmap.obj()) < 0) {
    LOG(ERROR) << "Error unlocking java bitmap pixels.";
    return false;
  }

  return succeeded;
}

bool RenderPictureToCanvas(SkPicture* picture, SkCanvas* canvas) {
  canvas->drawPicture(*picture);
  return true;
}

}  // namespace

InProcessViewRenderer::InProcessViewRenderer(
    BrowserViewRenderer::Client* client,
    JavaHelper* java_helper)
    : client_(client),
      java_helper_(java_helper),
      web_contents_(NULL),
      compositor_(NULL),
      view_visible_(false),
      continuous_invalidate_(false),
      continuous_invalidate_task_pending_(false),
      width_(0),
      height_(0),
      attached_to_window_(false),
      hardware_initialized_(false),
      hardware_failed_(false),
      egl_context_at_init_(NULL),
      weak_factory_(this) {
}

InProcessViewRenderer::~InProcessViewRenderer() {
  if (compositor_)
    compositor_->SetClient(NULL);
  SetContents(NULL);
}

// static
InProcessViewRenderer* InProcessViewRenderer::FromWebContents(
    content::WebContents* contents) {
  return UserData::GetInstance(contents);
}

// static
InProcessViewRenderer* InProcessViewRenderer::FromId(int render_process_id,
                                                     int render_view_id) {
  const content::RenderViewHost* rvh =
      content::RenderViewHost::FromID(render_process_id, render_view_id);
  if (!rvh) return NULL;
  return InProcessViewRenderer::FromWebContents(
      content::WebContents::FromRenderViewHost(rvh));
}

void InProcessViewRenderer::BindSynchronousCompositor(
    content::SynchronousCompositor* compositor) {
  DCHECK(compositor && compositor_ != compositor);
  if (compositor_)
    compositor_->SetClient(NULL);
  compositor_ = compositor;
  hardware_initialized_ = false;
  hardware_failed_ = false;
  compositor_->SetClient(this);

  if (attached_to_window_)
    client_->RequestProcessMode();
}

void InProcessViewRenderer::SetContents(
    content::ContentViewCore* content_view_core) {
  // First remove association from the prior ContentViewCore / WebContents.
  if (web_contents_) {
    web_contents_->SetUserData(kUserDataKey, NULL);
    DCHECK(!web_contents_);  // WebContentsGone should have been called.
  }

  if (!content_view_core)
    return;

  web_contents_ = content_view_core->GetWebContents();
  web_contents_->SetUserData(kUserDataKey, new UserData(this));
}

void InProcessViewRenderer::WebContentsGone() {
  web_contents_ = NULL;
}

bool InProcessViewRenderer::PrepareDrawGL(int x, int y) {
  // No harm in updating |hw_rendering_scroll_| even if we return false.
  hw_rendering_scroll_ = gfx::Point(x, y);
  return attached_to_window_ && compositor_ && compositor_->IsHwReady() &&
      !hardware_failed_;
}

void InProcessViewRenderer::DrawGL(AwDrawGLInfo* draw_info) {
  DCHECK(view_visible_);

  // We need to watch if the current Android context has changed and enforce
  // a clean-up in the compositor.
  EGLContext current_context = eglGetCurrentContext();
  if (!current_context) {
    LOG(WARNING) << "No current context attached. Skipping composite.";
    return;
  }

  GLStateRestore state_restore;

  if (attached_to_window_ && compositor_ && !hardware_initialized_) {
    // TODO(boliu): Actually initialize the compositor GL path.
    hardware_initialized_ = true;
    egl_context_at_init_ = current_context;
  }

  if (draw_info->mode == AwDrawGLInfo::kModeProcess)
    return;

  if (egl_context_at_init_ != current_context) {
    // TODO(boliu): Handle context lost
  }

  // TODO(boliu): Make sure this is not called before compositor is initialized
  // and GL is ready. Then make this a DCHECK.
  if (!compositor_)
    return;

  gfx::Transform transform;
  transform.matrix().setColMajorf(draw_info->transform);
  transform.Translate(hw_rendering_scroll_.x(), hw_rendering_scroll_.y());
  // TODO(joth): Check return value.
  compositor_->DemandDrawHw(
      gfx::Size(draw_info->width, draw_info->height),
      transform,
      gfx::Rect(draw_info->clip_left,
                draw_info->clip_top,
                draw_info->clip_right - draw_info->clip_left,
                draw_info->clip_bottom - draw_info->clip_top));

  EnsureContinuousInvalidation();
}

bool InProcessViewRenderer::DrawSW(jobject java_canvas,
                                   const gfx::Rect& clip) {
  bool result = DrawSWInternal(java_canvas, clip);
  EnsureContinuousInvalidation();
  return result;
}

bool InProcessViewRenderer::DrawSWInternal(jobject java_canvas,
                                           const gfx::Rect& clip) {
  TRACE_EVENT0("android_webview", "InProcessViewRenderer::DrawSW");

  if (clip.IsEmpty()) {
    TRACE_EVENT_INSTANT0("android_webview", "Empty Clip",
                         TRACE_EVENT_SCOPE_THREAD);
    return true;
  }

  JNIEnv* env = AttachCurrentThread();

  AwDrawSWFunctionTable* sw_functions = GetAwDrawSWFunctionTable();
  AwPixelInfo* pixels = sw_functions ?
      sw_functions->access_pixels(env, java_canvas) : NULL;
  // Render into an auxiliary bitmap if pixel info is not available.
  if (pixels == NULL) {
    TRACE_EVENT0("android_webview", "Render to Aux Bitmap");
    ScopedJavaLocalRef<jobject> jbitmap(java_helper_->CreateBitmap(
        env, clip.width(), clip.height(), true));
    if (!jbitmap.obj()) {
      TRACE_EVENT_INSTANT0("android_webview", "Bitmap Alloc Fail",
                           TRACE_EVENT_SCOPE_THREAD);
      return false;
    }

    if (!RasterizeIntoBitmap(env, jbitmap, clip.x(), clip.y(),
                             base::Bind(&InProcessViewRenderer::RenderSW,
                                        base::Unretained(this)))) {
      TRACE_EVENT_INSTANT0("android_webview", "Rasterize Fail",
                           TRACE_EVENT_SCOPE_THREAD);
      return false;
    }

    ScopedJavaLocalRef<jobject> jcanvas(env, java_canvas);
    java_helper_->DrawBitmapIntoCanvas(env, jbitmap, jcanvas);
    return true;
  }

  // Draw in a SkCanvas built over the pixel information.
  bool succeeded = false;
  {
    SkBitmap bitmap;
    bitmap.setConfig(static_cast<SkBitmap::Config>(pixels->config),
                     pixels->width,
                     pixels->height,
                     pixels->row_bytes);
    bitmap.setPixels(pixels->pixels);
    SkDevice device(bitmap);
    SkCanvas canvas(&device);
    SkMatrix matrix;
    for (int i = 0; i < 9; i++)
      matrix.set(i, pixels->matrix[i]);
    canvas.setMatrix(matrix);

    SkRegion clip;
    if (pixels->clip_region_size) {
      size_t bytes_read = clip.readFromMemory(pixels->clip_region);
      DCHECK_EQ(pixels->clip_region_size, bytes_read);
      canvas.setClipRegion(clip);
    } else {
      clip.setRect(SkIRect::MakeWH(pixels->width, pixels->height));
    }

    succeeded = RenderSW(&canvas);
  }

  sw_functions->release_pixels(pixels);
  return succeeded;
}

base::android::ScopedJavaLocalRef<jobject>
InProcessViewRenderer::CapturePicture() {
  if (!GetAwDrawSWFunctionTable())
    return ScopedJavaLocalRef<jobject>();

  gfx::Size record_size(width_, height_);

  // Return empty Picture objects for empty SkPictures.
  JNIEnv* env = AttachCurrentThread();
  if (record_size.width() <= 0 || record_size.height() <= 0) {
    return java_helper_->RecordBitmapIntoPicture(
        env, ScopedJavaLocalRef<jobject>());
  }

  skia::RefPtr<SkPicture> picture = skia::AdoptRef(new SkPicture);
  SkCanvas* rec_canvas = picture->beginRecording(record_size.width(),
                                                 record_size.height(),
                                                 0);
  if (!CompositeSW(rec_canvas))
    return ScopedJavaLocalRef<jobject>();
  picture->endRecording();

  if (IsSkiaVersionCompatible()) {
    // Add a reference that the create_picture() will take ownership of.
    picture->ref();
    return ScopedJavaLocalRef<jobject>(env,
        GetAwDrawSWFunctionTable()->create_picture(env, picture.get()));
  }

  // If Skia versions are not compatible, workaround it by rasterizing the
  // picture into a bitmap and drawing it into a new Java picture. Pass false
  // for |cache_result| as the picture we create will hold a shallow reference
  // to the bitmap drawn, and we don't want subsequent draws to corrupt any
  // previously returned pictures.
  ScopedJavaLocalRef<jobject> jbitmap(java_helper_->CreateBitmap(
      env, picture->width(), picture->height(), false));
  if (!jbitmap.obj())
    return ScopedJavaLocalRef<jobject>();

  if (!RasterizeIntoBitmap(env, jbitmap, 0, 0,
      base::Bind(&RenderPictureToCanvas,
                 base::Unretained(picture.get())))) {
    return ScopedJavaLocalRef<jobject>();
  }

  return java_helper_->RecordBitmapIntoPicture(env, jbitmap);
}

void InProcessViewRenderer::EnableOnNewPicture(bool enabled) {
}

void InProcessViewRenderer::OnVisibilityChanged(bool view_visible,
                                                bool window_visible) {
  view_visible_ = window_visible && view_visible;
}

void InProcessViewRenderer::OnSizeChanged(int width, int height) {
  width_ = width;
  height_ = height;
}

void InProcessViewRenderer::OnAttachedToWindow(int width, int height) {
  attached_to_window_ = true;
  width_ = width;
  height_ = height;
  if (compositor_ && !hardware_initialized_)
    client_->RequestProcessMode();
}

void InProcessViewRenderer::OnDetachedFromWindow() {
  // TODO(joth): Release GL resources. crbug.com/231986.
  attached_to_window_ = false;
}

bool InProcessViewRenderer::IsAttachedToWindow() {
  return attached_to_window_;
}

bool InProcessViewRenderer::IsViewVisible() {
  return view_visible_;
}

gfx::Rect InProcessViewRenderer::GetScreenRect() {
  return gfx::Rect(client_->GetLocationOnScreen(), gfx::Size(width_, height_));
}

void InProcessViewRenderer::DidDestroyCompositor(
    content::SynchronousCompositor* compositor) {
  // Allow for transient hand-over when two compositors may reference
  // a single client.
  if (compositor_ == compositor)
    compositor_ = NULL;
}

void InProcessViewRenderer::SetContinuousInvalidate(bool invalidate) {
  if (continuous_invalidate_ == invalidate)
    return;

  continuous_invalidate_ = invalidate;
  // TODO(boliu): Handle if not attached to window case.
  EnsureContinuousInvalidation();
}

void InProcessViewRenderer::Invalidate() {
  continuous_invalidate_task_pending_ = false;
  if (continuous_invalidate_)
    client_->Invalidate();
}

void InProcessViewRenderer::EnsureContinuousInvalidation() {
  if (continuous_invalidate_ && !continuous_invalidate_task_pending_) {
    MessageLoop::current()->PostTask(FROM_HERE,
        base::Bind(&InProcessViewRenderer::Invalidate,
                   weak_factory_.GetWeakPtr()));
    continuous_invalidate_task_pending_ = true;
  }
}

bool InProcessViewRenderer::RenderSW(SkCanvas* canvas) {
  // TODO(joth): BrowserViewRendererImpl had a bunch of logic for dpi and page
  // scale here. Determine what if any needs bringing over to this class.
  return CompositeSW(canvas);
}

bool InProcessViewRenderer::CompositeSW(SkCanvas* canvas) {
  return compositor_ && compositor_->DemandDrawSw(canvas);
}

}  // namespace android_webview
