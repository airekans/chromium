# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'cc_source_files': [
      'animation/animation.cc',
      'animation/animation.h',
      'animation/animation_curve.cc',
      'animation/animation_curve.h',
      'animation/animation_events.h',
      'animation/animation_id_provider.cc',
      'animation/animation_id_provider.h',
      'animation/animation_registrar.cc',
      'animation/animation_registrar.h',
      'append_quads_data.h',
      'bitmap_content_layer_updater.cc',
      'bitmap_content_layer_updater.h',
      'bitmap_skpicture_content_layer_updater.cc',
      'bitmap_skpicture_content_layer_updater.h',
      'caching_bitmap_content_layer_updater.cc',
      'caching_bitmap_content_layer_updater.h',
      'quads/checkerboard_draw_quad.cc',
      'quads/checkerboard_draw_quad.h',
      'base/completion_event.h',
      'output/compositor_frame.cc',
      'output/compositor_frame.h',
      'output/compositor_frame_ack.cc',
      'output/compositor_frame_ack.h',
      'output/compositor_frame_metadata.cc',
      'output/compositor_frame_metadata.h',
      'content_layer.cc',
      'content_layer.h',
      'content_layer_client.h',
      'content_layer_updater.cc',
      'content_layer_updater.h',
      'contents_scaling_layer.cc',
      'contents_scaling_layer.h',
      'output/context_provider.h',
      'damage_tracker.cc',
      'damage_tracker.h',
      'quads/debug_border_draw_quad.cc',
      'quads/debug_border_draw_quad.h',
      'debug/debug_colors.cc',
      'debug/debug_colors.h',
      'debug/debug_rect_history.cc',
      'debug/debug_rect_history.h',
      'delay_based_time_source.cc',
      'delay_based_time_source.h',
      'output/delegated_frame_data.h',
      'output/delegated_frame_data.cc',
      'delegated_renderer_layer.cc',
      'delegated_renderer_layer.h',
      'delegated_renderer_layer_impl.cc',
      'delegated_renderer_layer_impl.h',
      'output/delegating_renderer.cc',
      'output/delegating_renderer.h',
      'debug/devtools_instrumentation.h',
      'output/direct_renderer.cc',
      'output/direct_renderer.h',
      'draw_properties.h',
      'quads/draw_quad.cc',
      'quads/draw_quad.h',
      'debug/fake_web_graphics_context_3d.cc',
      'debug/fake_web_graphics_context_3d.h',
      'frame_rate_controller.cc',
      'frame_rate_controller.h',
      'debug/frame_rate_counter.cc',
      'debug/frame_rate_counter.h',
      'output/geometry_binding.cc',
      'output/geometry_binding.h',
      'output/gl_frame_data.h',
      'output/gl_frame_data.cc',
      'output/gl_renderer.cc',
      'output/gl_renderer.h',
      'output/gl_renderer_draw_cache.cc',
      'output/gl_renderer_draw_cache.h',
      'base/hash_pair.h',
      'heads_up_display_layer.cc',
      'heads_up_display_layer.h',
      'heads_up_display_layer_impl.cc',
      'heads_up_display_layer_impl.h',
      'image_layer_updater.cc',
      'image_layer_updater.h',
      'image_layer.cc',
      'image_layer.h',
      'input/input_handler.h',
      'quads/io_surface_draw_quad.cc',
      'quads/io_surface_draw_quad.h',
      'io_surface_layer.cc',
      'io_surface_layer.h',
      'io_surface_layer_impl.cc',
      'io_surface_layer_impl.h',
      'animation/keyframed_animation_curve.cc',
      'animation/keyframed_animation_curve.h',
      'latency_info.h',
      'layer.cc',
      'layer.h',
      'animation/layer_animation_controller.cc',
      'animation/layer_animation_controller.h',
      'animation/layer_animation_event_observer.h',
      'animation/layer_animation_value_observer.h',
      'layer_impl.cc',
      'layer_impl.h',
      'layer_iterator.cc',
      'layer_iterator.h',
      'layer_painter.h',
      'layer_quad.cc',
      'layer_quad.h',
      'layer_sorter.cc',
      'layer_sorter.h',
      'layer_tiling_data.cc',
      'layer_tiling_data.h',
      'debug/layer_tree_debug_state.cc',
      'debug/layer_tree_debug_state.h',
      'layer_tree_host.cc',
      'layer_tree_host.h',
      'layer_tree_host_client.h',
      'layer_tree_host_common.cc',
      'layer_tree_host_common.h',
      'layer_tree_host_impl.cc',
      'layer_tree_host_impl.h',
      'layer_tree_impl.cc',
      'layer_tree_impl.h',
      'layer_tree_settings.cc',
      'layer_tree_settings.h',
      'layer_updater.cc',
      'layer_updater.h',
      'managed_memory_policy.cc',
      'managed_memory_policy.h',
      'managed_tile_state.cc',
      'managed_tile_state.h',
      'base/math_util.cc',
      'base/math_util.h',
      'memory_history.cc',
      'memory_history.h',
      'nine_patch_layer.cc',
      'nine_patch_layer.h',
      'nine_patch_layer_impl.cc',
      'nine_patch_layer_impl.h',
      'occlusion_tracker.cc',
      'occlusion_tracker.h',
      'output/output_surface.cc',
      'output/output_surface.h',
      'output/output_surface_client.h',
      'debug/overdraw_metrics.cc',
      'debug/overdraw_metrics.h',
      'input/page_scale_animation.cc',
      'input/page_scale_animation.h',
      'debug/paint_time_counter.cc',
      'debug/paint_time_counter.h',
      'picture.cc',
      'picture.h',
      'picture_image_layer.cc',
      'picture_image_layer.h',
      'picture_image_layer_impl.cc',
      'picture_image_layer_impl.h',
      'picture_layer.cc',
      'picture_layer.h',
      'picture_layer_impl.cc',
      'picture_layer_impl.h',
      'picture_layer_tiling.cc',
      'picture_layer_tiling.h',
      'picture_layer_tiling_set.cc',
      'picture_layer_tiling_set.h',
      'picture_pile.cc',
      'picture_pile.h',
      'picture_pile_base.cc',
      'picture_pile_base.h',
      'picture_pile_impl.cc',
      'picture_pile_impl.h',
      'pinch_zoom_scrollbar.cc',
      'pinch_zoom_scrollbar.h',
      'pinch_zoom_scrollbar_geometry.cc',
      'pinch_zoom_scrollbar_geometry.h',
      'pinch_zoom_scrollbar_painter.cc',
      'pinch_zoom_scrollbar_painter.h',
      'platform_color.h',
      'prioritized_resource.cc',
      'prioritized_resource.h',
      'prioritized_resource_manager.cc',
      'prioritized_resource_manager.h',
      'priority_calculator.cc',
      'priority_calculator.h',
      'output/program_binding.cc',
      'output/program_binding.h',
      'proxy.cc',
      'proxy.h',
      'quad_culler.cc',
      'quad_culler.h',
      'quad_sink.h',
      'raster_worker_pool.cc',
      'raster_worker_pool.h',
      'rate_limiter.cc',
      'rate_limiter.h',
      'base/region.cc',
      'base/region.h',
      'quads/render_pass.cc',
      'quads/render_pass.h',
      'quads/render_pass_draw_quad.cc',
      'quads/render_pass_draw_quad.h',
      'render_pass_sink.h',
      'render_surface.cc',
      'render_surface.h',
      'output/render_surface_filters.cc',
      'output/render_surface_filters.h',
      'render_surface_impl.cc',
      'render_surface_impl.h',
      'output/renderer.cc',
      'output/renderer.h',
      'debug/rendering_stats.cc',
      'debug/rendering_stats.h',
      'resource.cc',
      'resource.h',
      'resource_pool.cc',
      'resource_pool.h',
      'resource_provider.cc',
      'resource_provider.h',
      'resource_update.cc',
      'resource_update.h',
      'resource_update_controller.cc',
      'resource_update_controller.h',
      'resource_update_queue.cc',
      'resource_update_queue.h',
      'debug/ring_buffer.h',
      'scheduler.cc',
      'scheduler.h',
      'scheduler_settings.cc',
      'scheduler_settings.h',
      'scheduler_state_machine.cc',
      'scheduler_state_machine.h',
      'base/scoped_ptr_algorithm.h',
      'base/scoped_ptr_deque.h',
      'base/scoped_ptr_hash_map.h',
      'base/scoped_ptr_vector.h',
      'scoped_resource.cc',
      'scoped_resource.h',
      'animation/scrollbar_animation_controller.h',
      'animation/scrollbar_animation_controller_linear_fade.cc',
      'animation/scrollbar_animation_controller_linear_fade.h',
      'scrollbar_geometry_fixed_thumb.cc',
      'scrollbar_geometry_fixed_thumb.h',
      'scrollbar_geometry_stub.cc',
      'scrollbar_geometry_stub.h',
      'scrollbar_layer.cc',
      'scrollbar_layer.h',
      'scrollbar_layer_impl.cc',
      'scrollbar_layer_impl.h',
      'scrollbar_layer_impl_base.h',
      'output/shader.cc',
      'output/shader.h',
      'quads/shared_quad_state.cc',
      'quads/shared_quad_state.h',
      'single_thread_proxy.cc',
      'single_thread_proxy.h',
      'skpicture_content_layer_updater.cc',
      'skpicture_content_layer_updater.h',
      'output/software_frame_data.cc',
      'output/software_frame_data.h',
      'output/software_output_device.cc',
      'output/software_output_device.h',
      'output/software_renderer.cc',
      'output/software_renderer.h',
      'quads/solid_color_draw_quad.cc',
      'quads/solid_color_draw_quad.h',
      'solid_color_layer.cc',
      'solid_color_layer.h',
      'solid_color_layer_impl.cc',
      'solid_color_layer_impl.h',
      'quads/stream_video_draw_quad.cc',
      'quads/stream_video_draw_quad.h',
      'base/switches.cc',
      'base/switches.h',
      'output/texture_copier.cc',
      'output/texture_copier.h',
      'quads/texture_draw_quad.cc',
      'quads/texture_draw_quad.h',
      'texture_layer.cc',
      'texture_layer.h',
      'texture_layer_client.h',
      'texture_layer_impl.cc',
      'texture_layer_impl.h',
      'texture_mailbox.cc',
      'texture_mailbox.h',
      'texture_uploader.cc',
      'texture_uploader.h',
      'base/thread.h',
      'base/thread_impl.cc',
      'base/thread_impl.h',
      'thread_proxy.cc',
      'thread_proxy.h',
      'tile.cc',
      'tile.h',
      'quads/tile_draw_quad.cc',
      'quads/tile_draw_quad.h',
      'tile_manager.cc',
      'tile_manager.h',
      'tile_priority.cc',
      'tile_priority.h',
      'tiled_layer.cc',
      'tiled_layer.h',
      'tiled_layer_impl.cc',
      'tiled_layer_impl.h',
      'base/tiling_data.cc',
      'base/tiling_data.h',
      'time_source.h',
      'timing_function.cc',
      'timing_function.h',
      'input/top_controls_manager.cc',
      'input/top_controls_manager.h',
      'input/top_controls_manager_client.h',
      'transferable_resource.cc',
      'transferable_resource.h',
      'transform_operation.cc',
      'transform_operation.h',
      'transform_operations.cc',
      'transform_operations.h',
      'tree_synchronizer.cc',
      'tree_synchronizer.h',
      'base/util.h',
      'video_frame_provider.h',
      'video_frame_provider_client_impl.cc',
      'video_frame_provider_client_impl.h',
      'video_layer.cc',
      'video_layer.h',
      'video_layer_impl.cc',
      'video_layer_impl.h',
      'vsync_time_source.cc',
      'vsync_time_source.h',
      'base/worker_pool.cc',
      'base/worker_pool.h',
      'quads/yuv_video_draw_quad.cc',
      'quads/yuv_video_draw_quad.h',
    ],
    'conditions': [
      ['inside_chromium_build==1', {
        'webkit_src_dir': '<(DEPTH)/third_party/WebKit',
      }, {
        'webkit_src_dir': '<(DEPTH)/../../..',
      }],
    ],
  },
  'conditions': [
    ['inside_chromium_build==0', {
      'defines': [
        'INSIDE_WEBKIT_BUILD=1',
      ],
    }],
  ],
  'targets': [
    {
      'target_name': 'cc',
      'type': '<(component)',
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
        '<(DEPTH)/gpu/gpu.gyp:gpu',
        '<(DEPTH)/skia/skia.gyp:skia',
        '<(DEPTH)/media/media.gyp:media',
        '<(DEPTH)/ui/gl/gl.gyp:gl',
        '<(DEPTH)/ui/surface/surface.gyp:surface',
        '<(DEPTH)/ui/ui.gyp:ui',
        '<(webkit_src_dir)/Source/WebKit/chromium/WebKit.gyp:webkit',
      ],
      'defines': [
        'CC_IMPLEMENTATION=1',
      ],
      'sources': [
        '<@(cc_source_files)',
      ],
      # TODO(jschuh): crbug.com/167187 fix size_t to int truncations.
      'msvs_disabled_warnings': [ 4267, ],
    },
  ],
}
