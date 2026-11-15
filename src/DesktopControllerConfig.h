#ifndef DESKTOP_CONTROLLER_CONFIG_H
#define DESKTOP_CONTROLLER_CONFIG_H

#include <SDL.h>
#include <array>
#include <string>
#include <vector>

// Captures raw SDL joystick inputs and persists explicit SDL controller mappings.
// Rendering stays in the game's existing text UI.
class DesktopControllerConfig {
  public:
    DesktopControllerConfig() = default;
    ~DesktopControllerConfig();
    DesktopControllerConfig(const DesktopControllerConfig&) = delete;
    DesktopControllerConfig& operator=(const DesktopControllerConfig&) = delete;

    bool Initialize(const char* mappingsPath = nullptr);
    void Toggle();
    void Close();
    bool IsOpen() const { return open; }
    bool HandleEvent(const SDL_Event& event);
    unsigned int Revision() const { return revision; }
    SDL_JoystickID SelectedInstance() const;
    std::vector<std::string> Lines() const;

  private:
    void RefreshDevice();
    void SelectDevice(int deviceIndex);
    void SnapshotInputs();
    void BeginBinding();
    void Capture(const std::string& binding);
    bool SaveMapping();
    void Error(const std::string& message);

    bool open = false;
    SDL_Joystick* joystick = nullptr;
    int action = -1;
    unsigned int revision = 0;
    std::string mappingsPath;
    std::string status;
    std::array<std::string, 5> bindings;
    std::vector<Sint16> previousAxes;
    std::vector<Uint8> previousButtons;
    std::vector<Uint8> previousHats;
};

#endif
