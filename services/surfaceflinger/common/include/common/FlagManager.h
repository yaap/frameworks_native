/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>

namespace android {
// Manages flags for SurfaceFlinger, including default values, system properties, and Mendel
// experiment configuration values. Can be called from any thread.
class FlagManager {
public:
    static const FlagManager& getInstance();
    static FlagManager& getMutableInstance();

    virtual ~FlagManager();

    void markBootCompleted();
    void dump(std::string& result) const;

    void setUnitTestMode();

    /// Debug sysprop flags ///
    bool disable_sched_fifo_sf() const;
    bool disable_sched_fifo_sf_binder() const;
    bool disable_sched_fifo_sf_sched() const;
    bool disable_sched_fifo_re() const;
    bool disable_sched_fifo_composer() const;
    bool disable_sched_fifo_composer_callback() const;
    bool force_agtm_without_luts() const;
    bool invalid_hdr_type_for_force_sdr_optin() const;
    bool productionize_readback_screenshot() const;
    bool stable_edid_ids_for_external_displays_optin() const;

    /// Legacy server flags ///
    bool test_flag() const;
    bool use_adpf_cpu_hint() const;
    bool use_skia_tracing() const;

    /// Trunk stable server (R/W) flags ///
    /// IMPORTANT - please keep alphabetized to reduce merge conflicts
    bool adpf_gpu_sf() const;
    bool align_adpf_with_sf_opt_policy() const;
    bool bugfix_layer_caching_color_inversion_flickering() const;
    bool bugfix_resize_virtual_display_surfaces() const;
    bool bugfix_virtual_display_refresh_rate() const;
    bool color_transform_box_shadows_and_border() const;
    bool color_transform_translation() const;
    bool configure_work_duration() const;
    bool debug_gpu_present_times() const;
    bool disable_transparent_region_hint() const;
    bool enable_color_correction_bugfix() const;
    bool fence_handling() const;
    bool force_sdr_invalid_hdr_type() const;
    bool frontend_caching_v0() const;
    bool frametimeline_boottime_in_lambda() const;
    bool get_display_known_vsync_sample_enabled() const;
    bool graphite_renderengine_preview_rollout() const;
    bool graphite_renderengine_preview2_rollout() const;
    bool graphite_renderengine_desktop_rollout() const;
    bool hwc_buffer_override_skip() const;
    bool md_degrade_hdr() const;
    bool mirror_uid_filtering() const;
    bool mirror_with_crop() const;
    bool monitor_buffer_fences() const;
    bool mrr_full_frame_rate_list() const;
    bool offload_gpu_composition() const;
    bool re_powered_off_displays_inform_cache_budgets() const;
    bool readback_screenshot() const;
    bool refresh_rate_overlay_on_external_display() const;
    bool set_power_mode_async() const;
    bool use_content_priority_for_jank_classification() const;
    bool use_experimental_jank_classification() const;
    bool use_last_vsync_predict() const;
    bool vd_aware_scheduler() const;
    bool virtual_display_content_filtering() const;

    /// Trunk stable readonly flags ///
    /// IMPORTANT - please keep alphabetize to reduce merge conflicts
    bool cache_when_source_crop_layer_only_moved() const;
    bool connected_display_hdr_v2() const;
    bool connected_display_hdr_v3() const;
    bool correct_dpi_with_display_size() const;
    bool display_command_modeset() const;
    bool enable_user_preferred_hdr_mode() const;
    bool frame_rate_category_mrr() const;
    bool graphite_renderengine() const;
    bool idle_screen_refresh_rate_timeout() const;
    bool local_tonemap_screenshots() const;
    bool luts_api() const;
    bool modeset_multi_display() const;
    bool modeset_state_machine() const;
    bool no_vsyncs_on_screen_off() const;
    bool parse_edid_version_and_input_type_v2() const;
    bool protected_if_client() const;
    bool renderable_buffer_usage() const;
    bool restore_blur_step() const;
    bool shader_disk_cache() const;
    bool small_blur_region_improvements() const;
    bool skip_invisible_windows_in_input() const;
    bool stable_edid_ids() const;
    bool synced_resolution_switch() const;
    bool true_hdr_screenshots() const;
    bool vulkan_renderengine() const;
    bool wb_framebuffersurface2() const;
    bool wb_virtualdisplay2() const;
    bool window_blur_kawase2_preallocate_buffers() const;
    /// IMPORTANT - please keep alphabetize to reduce merge conflicts

    bool follower_arbitrary_refresh_rate_selection_combined() const;
    bool follower_display_backpressure_combined() const;
    bool force_slower_follower_gpu_composition_combined() const;

protected:
    // overridden for unit tests
    virtual std::optional<bool> getBoolProperty(const char*) const;
    virtual bool getServerConfigurableFlag(const char*) const;

private:
    friend class TestableFlagManager;

    FlagManager() = default;
    FlagManager(const FlagManager&) = delete;

    bool follower_arbitrary_refresh_rate_selection() const;
    bool follower_arbitrary_refresh_rate_selection_platform() const;
    bool follower_display_backpressure() const;
    bool follower_display_backpressure_platform() const;
    bool force_slower_follower_gpu_composition() const;
    bool force_slower_follower_gpu_composition_platform() const;

    void dumpFlag(std::string& result, bool readonly, const char* name,
                  std::function<bool()> getter) const;

    std::atomic_bool mBootCompleted = false;
    std::atomic_bool mUnitTestMode = false;
};
} // namespace android
