#pragma once
#include "common/image.h"
#include "settings.h"
#include "types.h"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class StateWrapper;
class HostInterface;

class Controller
{
public:
  enum class AxisType : uint8_t
  {
    Full,
    Half
  };

  using ButtonList = std::vector<std::pair<std::string, int32_t>>;
  using AxisList = std::vector<std::tuple<std::string, int32_t, AxisType>>;
  using SettingList = std::vector<SettingInfo>;

  Controller();
  virtual ~Controller();

  /// Returns the type of controller.
  virtual ControllerType GetType() const = 0;

  virtual void Reset();
  virtual bool DoState(StateWrapper& sw, bool apply_input_state);

  // Resets all state for the transferring to/from the device.
  virtual void ResetTransferState();

  // Returns the value of ACK, as well as filling out_data.
  virtual bool Transfer(const uint8_t data_in, uint8_t* data_out);

  /// Changes the specified axis state. Values are normalized from -1..1.
  virtual void SetAxisState(int32_t axis_code, float value);

  /// Changes the specified button state.
  virtual void SetButtonState(int32_t button_code, bool pressed);

  /// Returns a bitmask of the current button states, 1 = on.
  virtual uint32_t GetButtonStateBits() const;

  /// Returns analog input bytes packed as a uint32_t. Values are specific to controller type.
  virtual std::optional<uint32_t> GetAnalogInputBytes() const;

  /// Returns the number of vibration motors.
  virtual uint32_t GetVibrationMotorCount() const;

  /// Queries the state of the specified vibration motor. Values are normalized from 0..1.
  virtual float GetVibrationMotorStrength(uint32_t motor);

  /// Loads/refreshes any per-controller settings.
  virtual void LoadSettings(const char* section);

  /// Returns the software cursor to use for this controller, if any.
  virtual bool GetSoftwareCursor(const Common::RGBA8Image** image, float* image_scale, bool* relative_mode);

  /// Creates a new controller of the specified type.
  static std::unique_ptr<Controller> Create(ControllerType type, uint32_t index);

  /// Returns the number of vibration motors.
  static uint32_t GetVibrationMotorCount(ControllerType type);
};
