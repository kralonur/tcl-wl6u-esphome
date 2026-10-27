#pragma once

#include "esphome/core/defines.h"
#ifdef USE_CLIMATE

#include <algorithm>
#include <cmath>
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/climate/climate.h"
#include "tcl_ac.h"

namespace esphome {
namespace wl6u_ac {

// Home Assistant climate entity on top of TclAc. It publishes only what the
// AC reports in its status replies and never transmits on its own: each Home
// Assistant call becomes queued scalar changes (TclAc::request_property),
// sent as one frame once a fresh status has been read back.
class TclAcClimate : public climate::Climate, public Component {
 public:
  void set_hub(TclAc *hub) { hub_ = hub; }

  void setup() override {
    set_supported_custom_fan_modes(speeds_());
    hub_->set_on_status([this]() { this->on_status_(); });
  }

  void dump_config() override {
    static const char *const TAG = "tcl_ac.climate";
    LOG_CLIMATE("", "TCL AC climate", this);
  }

  climate::ClimateTraits traits() override {
    climate::ClimateTraits traits;
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
    traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT_COOL,
                                climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_DRY,
                                climate::CLIMATE_MODE_FAN_ONLY, climate::CLIMATE_MODE_HEAT});
    traits.set_supported_fan_modes({climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_QUIET});
    traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_BOTH,
                                      climate::CLIMATE_SWING_VERTICAL,
                                      climate::CLIMATE_SWING_HORIZONTAL});
    traits.set_supported_presets({climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_ECO,
                                  climate::CLIMATE_PRESET_BOOST, climate::CLIMATE_PRESET_SLEEP});
    traits.set_visual_min_temperature(16.0f);
    traits.set_visual_max_temperature(31.0f);
    traits.set_visual_target_temperature_step(0.5f);
    traits.set_visual_current_temperature_step(0.1f);
    return traits;
  }

  void control(const climate::ClimateCall &call) override {
    if (call.get_mode().has_value()) {
      const auto mode = *call.get_mode();
      if (mode == climate::CLIMATE_MODE_OFF) {
        hub_->request_property("powerSwitch", 0);
      } else {
        for (size_t work_mode = 0; work_mode < 5; ++work_mode) {
          if (modes_()[work_mode] == mode) {
            hub_->request_property("powerSwitch", 1);
            hub_->request_property("workMode", float(work_mode));
          }
        }
      }
    }
    if (call.get_target_temperature().has_value()) {
      const float target = std::round(*call.get_target_temperature() * 2.0f) / 2.0f;
      hub_->request_property("targetTemperature", std::min(31.0f, std::max(16.0f, target)));
    }
    if (call.get_fan_mode().has_value()) {
      if (*call.get_fan_mode() == climate::CLIMATE_FAN_QUIET) {
        hub_->request_property("silenceSwitch", 1);
      } else if (*call.get_fan_mode() == climate::CLIMATE_FAN_AUTO) {
        hub_->request_property("silenceSwitch", 0);
        hub_->request_property("windSpeed", 0);
      }
    }
    const StringRef custom = call.get_custom_fan_mode();
    for (size_t i = 0; !custom.empty() && i < 5; ++i) {
      if (custom == speeds_()[i]) {
        hub_->request_property("silenceSwitch", 0);
        hub_->request_property("windSpeed", float(2 + i));  // windSpeed 2..6
      }
    }
    if (call.get_swing_mode().has_value()) {
      const auto swing = *call.get_swing_mode();
      const bool both = swing == climate::CLIMATE_SWING_BOTH;
      hub_->request_property("verticalSwitch", both || swing == climate::CLIMATE_SWING_VERTICAL);
      hub_->request_property("horizontalSwitch",
                             both || swing == climate::CLIMATE_SWING_HORIZONTAL);
    }
    if (call.get_preset().has_value()) {
      const auto preset = *call.get_preset();
      hub_->request_property("ECO", preset == climate::CLIMATE_PRESET_ECO);
      hub_->request_property("turbo", preset == climate::CLIMATE_PRESET_BOOST);
      hub_->request_property("sleep", preset == climate::CLIMATE_PRESET_SLEEP);
    }
    // No optimistic publish: the entity changes when the AC reports it.
  }

 protected:
  TclAc *hub_{nullptr};
  bool published_{false};

  // Stock workMode 0..4 (auto, cool, dry, fan, heat).
  static const climate::ClimateMode *modes_() {
    static const climate::ClimateMode kModes[] = {
        climate::CLIMATE_MODE_HEAT_COOL, climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_DRY,
        climate::CLIMATE_MODE_FAN_ONLY, climate::CLIMATE_MODE_HEAT};
    return kModes;
  }

  // windSpeed 2..6, slowest to fastest.
  static const char *const (&speeds_())[5] {
    static const char *const kSpeeds[5] = {"Speed 1", "Speed 2", "Speed 3", "Speed 4", "Speed 5"};
    return kSpeeds;
  }

  template<typename T> static bool update_(T &field, const T &value) {
    if (field == value)
      return false;
    field = value;
    return true;
  }

  void on_status_() {
    const uint8_t *p = hub_->status();
    const auto s = ::tcl_ac::decode_settings(p, hub_->status_length());
    bool changed = false;
    if (!s.power) {
      changed |= update_(this->mode, climate::CLIMATE_MODE_OFF);
    } else if (s.work_mode < 5) {
      changed |= update_(this->mode, modes_()[s.work_mode]);
    }
    changed |= update_(this->target_temperature, s.target_temperature);
    // Raw tenths; stock currentTemperature truncates the same value to whole degrees.
    changed |= update_(this->current_temperature,
                       float(::tcl_ac::current_temperature_raw(p) - 200) * 0.1f);
    if (s.silence) {
      changed |= this->set_fan_mode_(climate::CLIMATE_FAN_QUIET);
    } else if (s.wind_speed == 0) {
      changed |= this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
    } else if (s.wind_speed >= 2 && s.wind_speed <= 6) {
      changed |= this->set_custom_fan_mode_(speeds_()[s.wind_speed - 2]);
    }
    const auto swing = s.vertical_swing
                           ? (s.horizontal_swing ? climate::CLIMATE_SWING_BOTH
                                                 : climate::CLIMATE_SWING_VERTICAL)
                           : (s.horizontal_swing ? climate::CLIMATE_SWING_HORIZONTAL
                                                 : climate::CLIMATE_SWING_OFF);
    changed |= update_(this->swing_mode, swing);
    changed |= this->set_preset_(s.turbo  ? climate::CLIMATE_PRESET_BOOST
                                 : s.eco  ? climate::CLIMATE_PRESET_ECO
                                 : s.sleep ? climate::CLIMATE_PRESET_SLEEP
                                           : climate::CLIMATE_PRESET_NONE);
    if (changed || !published_) {
      published_ = true;
      this->publish_state();
    }
  }
};

}  // namespace wl6u_ac
}  // namespace esphome

#endif  // USE_CLIMATE
