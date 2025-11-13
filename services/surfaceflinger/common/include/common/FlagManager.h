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
    bool productionize_readback_screenshot() const;

    /// Legacy server flags ///
    bool test_flag() const;
    bool use_adpf_cpu_hint() const;
    bool use_skia_tracing() const;

    /// Trunk stable server (R/W) flags ///
    bool adpf_cpu_effects_loadup() const;
    bool adpf_gpu_sf() const;
    bool adpf_native_session_manager() const;
    bool adpf_use_fmq_channel() const;
    bool adpf_use_fmq_channel_fixed() const;
    bool correct_virtual_display_power_state() const;
    bool graphite_renderengine_preview_rollout() const;
    bool increase_missed_frame_jank_threshold() const;
    bool monitor_buffer_fences() const;
    bool readback_screenshot() const;
    bool refresh_rate_overlay_on_external_display() const;
    bool vsync_predictor_rate_change_with_aligned_sequence() const;
    bool unify_refresh_rate_callbacks() const;
    bool vsync_predictor_recovery() const;

    /// Trunk stable readonly flags ///
    /// IMPORTANT - please keep alphabetize to reduce merge conflicts
    bool adpf_fmq_sf() const;
    bool arr_setframerate_gte_enum() const;
    bool begone_bright_hlg() const;
    bool cache_when_source_crop_layer_only_moved() const;
    bool connected_display_hdr() const;
    bool correct_dpi_with_display_size() const;
    bool deprecate_frame_tracker() const;
    bool deprecate_vsync_sf() const;
    bool detached_mirror() const;
    bool disable_synthetic_vsync_for_performance() const;
    bool display_config_error_hal() const;
    bool enable_layer_command_batching() const;
    bool enable_small_area_detection() const;
    bool flush_buffer_slots_to_uncache() const;
    bool force_compile_graphite_renderengine() const;
    bool fp16_client_target() const;
    bool frame_rate_category_mrr() const;
    bool game_default_frame_rate() const;
    bool graphite_renderengine() const;
    bool hdcp_level_hal() const;
    bool hdcp_negotiation() const;
    bool idle_screen_refresh_rate_timeout() const;
    bool local_tonemap_screenshots() const;
    bool luts_api() const;
    bool no_vsyncs_on_screen_off() const;
    bool override_trusted_overlay() const;
    bool pacesetter_selection() const;
    bool protected_if_client() const;
    bool renderable_buffer_usage() const;
    bool restore_blur_step() const;
    bool skip_invisible_windows_in_input() const;
    bool stable_edid_ids() const;
    bool stop_layer() const;
    bool synced_resolution_switch() const;
    bool true_hdr_screenshots() const;
    bool use_known_refresh_rate_for_fps_consistency() const;
    bool vrr_config() const;
    bool vulkan_renderengine() const;
    bool wb_virtualdisplay2() const;
    bool window_blur_kawase2() const;
    /// IMPORTANT - please keep alphabetize to reduce merge conflicts

protected:
    // overridden for unit tests
    virtual std::optional<bool> getBoolProperty(const char*) const;
    virtual bool getServerConfigurableFlag(const char*) const;

private:
    friend class TestableFlagManager;

    FlagManager() = default;
    FlagManager(const FlagManager&) = delete;

    void dumpFlag(std::string& result, bool readonly, const char* name,
                  std::function<bool()> getter) const;

    std::atomic_bool mBootCompleted = false;
    std::atomic_bool mUnitTestMode = false;
};
} // namespace android
