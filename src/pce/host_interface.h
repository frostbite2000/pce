#pragma once
#include "YBaseLib/Barrier.h"
#include "YBaseLib/Semaphore.h"
#include "YBaseLib/String.h"
#include "YBaseLib/TaskQueue.h"
#include "YBaseLib/Timer.h"
#include "common/display.h"
#include "cpu.h"
#include "scancodes.h"
#include "system.h"
#include "types.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

class Error;

namespace Audio {
class Mixer;
}
class Component;
class System;
class TimingEvent;

class HostInterface
{
public:
  enum class IndicatorType : u8
  {
    None,
    FDD,
    HDD,
    CDROM,
    Serial
  };
  enum class IndicatorState : u8
  {
    Off,
    Reading,
    Writing
  };

  using ExternalEventCallback = std::function<void()>;

  HostInterface();
  virtual ~HostInterface();

  // System pointer, can be null.
  System* GetSystem() { return m_system.get(); }

  // Loads/creates a system.
  bool CreateSystem(const char* inifile, Error* error);

  // Resets the system.
  void ResetSystem();

  // Load/save state. If load fails, system is in an undefined state, Reset it.
  // This occurs asynchronously, the event maintains a reference to the stream.
  // The stream is committed upon success, or discarded upon fail.
  bool LoadSystemState(const char* filename, Error* error);
  void SaveSystemState(const char* filename);

  // External events, will interrupt the CPU and execute.
  // Use care when calling this variant, deadlocks can occur.
  void QueueExternalEvent(ExternalEventCallback callback, bool wait);

  // Safely changes CPU backend.
  // This change is done asynchronously.
  CPU::BackendType GetCPUBackend() const;
  float GetCPUFrequency() const;
  bool SetCPUBackend(CPU::BackendType backend);
  void SetCPUFrequency(float frequency);
  void FlushCPUCodeCache();

  // Speed limiter.
  bool IsSpeedLimiterEnabled() const { return m_speed_limiter_enabled; }
  void SetSpeedLimiterEnabled(bool enabled);

  // Simulation pausing/resuming/stopping.
  void PauseSimulation();
  void ResumeSimulation();
  void StopSimulation();

  // Display
  virtual DisplayRenderer* GetDisplayRenderer() const = 0;
  std::unique_ptr<Display> CreateDisplay(const char* name, Display::Type type, u8 priority = Display::DEFAULT_PRIORITY);

  // Audio
  virtual Audio::Mixer* GetAudioMixer() const = 0;

  // Remove all callbacks with this owner
  void RemoveAllCallbacks(const void* owner);

  // Keyboard
  using KeyboardCallback = std::function<void(GenScanCode scancode, bool key_down)>;
  void AddKeyboardCallback(const void* owner, KeyboardCallback callback);
  void InjectKeyEvent(GenScanCode sc, bool down);

  // Mouse
  using MousePositionChangeCallback = std::function<void(s32 dx, s32 dy)>;
  using MouseButtonChangeCallback = std::function<void(u32 button, bool state)>;
  void AddMousePositionChangeCallback(const void* owner, MousePositionChangeCallback callback);
  void AddMouseButtonChangeCallback(const void* owner, MouseButtonChangeCallback callback);

  // Error reporting. May block.
  virtual void ReportError(const char* message);
  void ReportFormattedError(const char* format, ...);

  // Status message logging.
  virtual void ReportMessage(const char* message);
  void ReportFormattedMessage(const char* format, ...);

  // Helper to check if the caller is on the simulation thread.
  bool IsOnSimulationThread() const;

  // Sends CTRL+ALT+DELETE to the simulated machine.
  void SendCtrlAltDel();

  // UI elements.
  using UICallback = std::function<void()>;
  using UIFileCallback = std::function<void(const String&)>;
  virtual void AddUIIndicator(const Component* component, IndicatorType type);
  virtual void SetUIIndicatorState(const Component* component, IndicatorState state);
  virtual void AddUICallback(const Component* component, const String& label, UICallback callback);
  virtual void AddUIFileCallback(const Component* component, const String& label, UIFileCallback callback);

  // Adds OSD messages, duration is in seconds.
  void AddOSDMessage(const char* message, float duration = 2.0f);

protected:
  struct ComponentUIElement
  {
    const Component* component;
    std::vector<std::pair<String, UICallback>> callbacks;
    std::vector<std::pair<String, std::function<void(const String&)>>> file_callbacks;
    IndicatorType indicator_type = IndicatorType::None;
    IndicatorState indicator_state = IndicatorState::Off;
  };

  struct SimulationStats
  {
    float simulation_speed;
    float host_cpu_usage;
    u64 total_time_simulated;
    u64 delta_time_simulated;
    CPU::ExecutionStats cpu_stats;
    u64 cpu_delta_cycles_executed;
    u64 cpu_delta_instructions_interpreted;
    u64 cpu_delta_exceptions_raised;
    u64 cpu_delata_interrupts_serviced;
    u64 cpu_delta_code_cache_blocks_executed;
    u64 cpu_delta_code_cache_instructions_executed;

    // TODO: Frames
  };

  struct OSDMessage
  {
    String text;
    Timer time;
    float duration;
  };

  // Implemented in derived classes.
  virtual void OnSystemInitialized();
  virtual void OnSystemReset();
  virtual void OnSystemStateLoaded();
  virtual void OnSystemDestroy();
  virtual void OnSimulationStatsUpdate(const SimulationStats& stats);
  virtual void OnSimulationResumed();
  virtual void OnSimulationPaused();

  // Yields execution, so that the main thread doesn't deadlock.
  virtual void YieldToUI();

  void ExecuteKeyboardCallbacks(GenScanCode scancode, bool key_down);
  void ExecuteMousePositionChangeCallbacks(s32 dx, s32 dy);
  void ExecuteMouseButtonChangeCallbacks(u32 button, bool state);

  // Simulation thread entry point.
  void SimulationThreadRoutine();
  void WaitForSimulationThread();
  void StopSimulationThread();

  ComponentUIElement* CreateComponentUIElement(const Component* component);
  ComponentUIElement* GetOrCreateComponentUIElement(const Component* component);
  ComponentUIElement* GetComponentUIElement(const Component* component);

  std::unique_ptr<System> m_system;
  std::vector<ComponentUIElement> m_component_ui_elements;
  std::deque<OSDMessage> m_osd_messages;
  std::mutex m_osd_messages_lock;

private:
  SimulationTime GetSimulationSliceTime() const;
  SimulationTime GetMaxSimulationVarianceTime() const;
  void HandleStateChange();
  bool ExecuteExternalEvents();
  void ThrottleEvent();
  void UpdateExecutionSpeed();
  void WaitForCallingThread();
  void ShutdownSystem();

  std::vector<std::pair<const void*, KeyboardCallback>> m_keyboard_callbacks;
  std::vector<std::pair<const void*, MousePositionChangeCallback>> m_mouse_position_change_callbacks;
  std::vector<std::pair<const void*, MouseButtonChangeCallback>> m_mouse_button_change_callbacks;

  // Throttle event
  std::unique_ptr<TimingEvent> m_throttle_event;
  Timer m_throttle_timer;
  u64 m_last_throttle_time = 0;
  bool m_speed_limiter_enabled = true;
  Timer m_speed_lost_time_timestamp;

  // Emulation speed tracking
  Timer m_speed_elapsed_real_time;
  SimulationTime m_speed_elapsed_simulation_time = 0;
  u64 m_speed_elapsed_user_time = 0;
  u64 m_speed_elapsed_kernel_time = 0;

  // Threaded running state
  std::thread::id m_simulation_thread_id;
  Barrier m_simulation_thread_barrier{2};
  Semaphore m_simulation_thread_semaphore;
  std::atomic_bool m_simulation_thread_running{true};
  System::State m_last_system_state = System::State::Stopped;

  // External event queue
  std::queue<std::pair<ExternalEventCallback, bool>> m_external_events;
  std::mutex m_external_events_lock;

  // Stats tracking
  CPU::ExecutionStats m_last_cpu_execution_stats = {};
};
