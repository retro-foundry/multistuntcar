#include "DesktopControllerConfig.h"

#include <sstream>

namespace {
const char* const actionNames[] = {"Steer left", "Steer right", "Accelerate / joystick up", "Brake / joystick down",
                                   "Boost / fire / menu confirm"};
const char* const sdlActions[] = {"dpleft", "dpright", "dpup", "dpdown", "a"};
const int axisThreshold = 16384;
const int actionCount = 5;
} // namespace

DesktopControllerConfig::~DesktopControllerConfig() { Close(); }

void DesktopControllerConfig::Error(const std::string& message) {
    status = message;
    SDL_LogError(SDL_LOG_CATEGORY_INPUT, "%s", message.c_str());
}

bool DesktopControllerConfig::Initialize(const char* path) {
    if (path) {
        mappingsPath = path;
    } else {
        char* directory = SDL_GetPrefPath("RetroFoundry", "MultiStuntCar");
        if (!directory) {
            Error(std::string("Cannot locate controller preferences: ") + SDL_GetError());
            return false;
        }
        mappingsPath = std::string(directory) + "controllers.txt";
        SDL_free(directory);
    }

    // a+b creates an empty preferences file on first use, without truncating
    // existing bindings. SDL_RWFromFile handles UTF-8 paths on Windows too.
    SDL_RWops* file = SDL_RWFromFile(mappingsPath.c_str(), "a+b");
    if (!file) {
        Error("Cannot open controller preferences " + mappingsPath + ": " + SDL_GetError());
        return false;
    }
    const Sint64 size = SDL_RWsize(file);
    if (size == 0) {
        SDL_RWclose(file);
        return true; // No saved mappings on first launch.
    }
    if (size < 0) {
        Error(std::string("Cannot read controller preferences: ") + SDL_GetError());
        SDL_RWclose(file);
        return false;
    }
    if (SDL_RWseek(file, 0, RW_SEEK_SET) < 0) {
        Error(std::string("Cannot read controller preferences: ") + SDL_GetError());
        SDL_RWclose(file);
        return false;
    }
    if (SDL_GameControllerAddMappingsFromRW(file, 1) < 0) {
        Error(std::string("Cannot load controller mappings: ") + SDL_GetError());
        return false;
    }
    return true;
}

void DesktopControllerConfig::Close() {
    if (joystick)
        SDL_JoystickClose(joystick);
    joystick = nullptr;
    open = false;
    action = -1;
}

void DesktopControllerConfig::Toggle() {
    if (open) {
        Close();
    } else {
        open = true;
        RefreshDevice();
    }
}

SDL_JoystickID DesktopControllerConfig::SelectedInstance() const {
    return joystick ? SDL_JoystickInstanceID(joystick) : -1;
}

void DesktopControllerConfig::SelectDevice(int deviceIndex) {
    if (joystick)
        SDL_JoystickClose(joystick);
    joystick = SDL_JoystickOpen(deviceIndex);
    action = -1;
    bindings.fill("");
    if (!joystick) {
        Error(std::string("Cannot open joystick for setup: ") + SDL_GetError());
        return;
    }
    char* mapping = SDL_GameControllerMappingForDeviceIndex(deviceIndex);
    if (mapping) {
        std::istringstream fields(mapping);
        std::string field;
        while (std::getline(fields, field, ','))
            for (int i = 0; i < actionCount; ++i) {
                const std::string prefix = std::string(sdlActions[i]) + ":";
                if (field.compare(0, prefix.size(), prefix) == 0)
                    bindings[i] = field.substr(prefix.size());
            }
        SDL_free(mapping);
    }
}

void DesktopControllerConfig::RefreshDevice() {
    if (joystick && SDL_JoystickGetAttached(joystick))
        return;
    if (joystick) {
        SDL_JoystickClose(joystick);
        joystick = nullptr;
        action = -1;
        status = "Controller disconnected; unfinished bindings were cancelled.";
    }
    if (SDL_NumJoysticks() > 0)
        SelectDevice(0);
}

void DesktopControllerConfig::SnapshotInputs() {
    previousAxes.resize(SDL_JoystickNumAxes(joystick));
    previousButtons.resize(SDL_JoystickNumButtons(joystick));
    previousHats.resize(SDL_JoystickNumHats(joystick));
    for (size_t i = 0; i < previousAxes.size(); ++i)
        previousAxes[i] = SDL_JoystickGetAxis(joystick, static_cast<int>(i));
    for (size_t i = 0; i < previousButtons.size(); ++i)
        previousButtons[i] = SDL_JoystickGetButton(joystick, static_cast<int>(i));
    for (size_t i = 0; i < previousHats.size(); ++i)
        previousHats[i] = SDL_JoystickGetHat(joystick, static_cast<int>(i));
}

void DesktopControllerConfig::BeginBinding() {
    if (!joystick) {
        status = "Connect a joystick, then press Enter to configure it.";
        return;
    }
    action = 0;
    bindings.fill("");
    SnapshotInputs();
    status = "Release each control before moving the next one. Axes, hats and buttons are supported.";
}

void DesktopControllerConfig::Capture(const std::string& binding) {
    bindings[action++] = binding;
    SnapshotInputs();
    if (action == actionCount)
        SaveMapping();
}

bool DesktopControllerConfig::SaveMapping() {
    if (!joystick || !SDL_JoystickGetAttached(joystick)) {
        Error("Cannot save bindings: the controller was disconnected.");
        return false;
    }
    if (mappingsPath.empty()) {
        Error("Cannot save bindings: the app preferences directory is unavailable.");
        return false;
    }
    char guid[33];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid, sizeof(guid));
    std::string mapping = std::string(guid) + ",Custom desktop joystick,";
    for (int i = 0; i < actionCount; ++i)
        mapping += std::string(sdlActions[i]) + ":" + bindings[i] + ",";
    mapping += std::string("platform:") + SDL_GetPlatform() + ",";

    // SDL loads later entries for the same GUID last. Append preserves other
    // devices' bindings and avoids truncating the preferences on a failed write.
    SDL_RWops* file = SDL_RWFromFile(mappingsPath.c_str(), "ab");
    if (!file) {
        Error("Cannot save controller preferences " + mappingsPath + ": " + SDL_GetError());
        return false;
    }
    const std::string line = "\n" + mapping + "\n";
    const bool written = SDL_RWwrite(file, line.data(), 1, line.size()) == line.size();
    const int closed = SDL_RWclose(file);
    if (!written || closed < 0) {
        Error(std::string("Cannot write controller preferences: ") + SDL_GetError());
        return false;
    }
    if (SDL_GameControllerAddMapping(mapping.c_str()) < 0) {
        Error(std::string("Cannot apply controller mapping: ") + SDL_GetError());
        return false;
    }
    ++revision;
    action = -1;
    status = "Controls saved. Esc or F3 returns to the game; Enter configures this device again.";
    return true;
}

bool DesktopControllerConfig::HandleEvent(const SDL_Event& event) {
    if (!open)
        return false;
    if (event.type == SDL_JOYDEVICEADDED || event.type == SDL_JOYDEVICEREMOVED) {
        RefreshDevice();
        return false; // The game must also handle controller connection events.
    }
    if (event.type == SDL_KEYDOWN) {
        if (event.key.repeat)
            return true;
        const SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_ESCAPE) {
            if (action >= 0) {
                action = -1;
                status = "Configuration cancelled; existing controls are unchanged.";
            } else {
                Close();
            }
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (action == actionCount)
                SaveMapping();
            else if (action < 0)
                BeginBinding();
        } else if (action < 0 && (key == SDLK_UP || key == SDLK_DOWN)) {
            const int count = SDL_NumJoysticks();
            for (int i = 0; i < count; ++i) {
                if (SDL_JoystickGetDeviceInstanceID(i) == SelectedInstance()) {
                    SelectDevice((i + (key == SDLK_DOWN ? 1 : count - 1)) % count);
                    status.clear();
                    break;
                }
            }
        }
        return true;
    }
    if (action >= 0 && action < actionCount && joystick) {
        if (event.type == SDL_JOYAXISMOTION && event.jaxis.which == SelectedInstance() &&
            event.jaxis.axis < previousAxes.size()) {
            const int value = event.jaxis.value;
            const int previous = previousAxes[event.jaxis.axis];
            previousAxes[event.jaxis.axis] = event.jaxis.value;
            if ((value <= -axisThreshold && previous > -axisThreshold) ||
                (value >= axisThreshold && previous < axisThreshold)) {
                Capture(std::string(value < 0 ? "-a" : "+a") + std::to_string(event.jaxis.axis));
            }
        } else if ((event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP) &&
                   event.jbutton.which == SelectedInstance() && event.jbutton.button < previousButtons.size()) {
            const bool wasPressed = previousButtons[event.jbutton.button] != 0;
            previousButtons[event.jbutton.button] = event.jbutton.state;
            if (event.jbutton.state == SDL_PRESSED && !wasPressed)
                Capture("b" + std::to_string(event.jbutton.button));
        } else if (event.type == SDL_JOYHATMOTION && event.jhat.which == SelectedInstance() &&
                   event.jhat.hat < previousHats.size()) {
            const Uint8 value = event.jhat.value;
            const Uint8 previous = previousHats[event.jhat.hat];
            previousHats[event.jhat.hat] = value;
            if (value != previous &&
                (value == SDL_HAT_UP || value == SDL_HAT_RIGHT || value == SDL_HAT_DOWN || value == SDL_HAT_LEFT)) {
                Capture("h" + std::to_string(event.jhat.hat) + "." + std::to_string(value));
            }
        }
    }
    return event.type == SDL_KEYUP || (event.type >= SDL_JOYAXISMOTION && event.type <= SDL_JOYBUTTONUP) ||
           event.type == SDL_CONTROLLERAXISMOTION || event.type == SDL_CONTROLLERBUTTONDOWN ||
           event.type == SDL_CONTROLLERBUTTONUP;
}

std::vector<std::string> DesktopControllerConfig::Lines() const {
    std::vector<std::string> lines = {"Controller setup", "", "Up/Down: choose device    Enter: configure",
                                      "Esc: cancel / close    F3: close", ""};
    if (!joystick) {
        lines.push_back("No joystick detected. Connect one to configure it.");
    } else {
        const char* name = SDL_JoystickName(joystick);
        lines.push_back(std::string("Device: ") + (name ? name : "Unnamed joystick"));
        int deviceIndex = -1;
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_JoystickGetDeviceInstanceID(i) == SelectedInstance())
                deviceIndex = i;
        lines.push_back(SDL_IsGameController(deviceIndex)
                            ? "SDL controller mapping available"
                            : "Generic joystick: configure its directions and fire below");
        lines.push_back("");
        for (int i = 0; i < actionCount; ++i)
            lines.push_back(std::string(action == i ? "> " : "  ") + actionNames[i] + ": " + bindings[i]);
        lines.push_back("");
        if (action >= 0 && action < actionCount)
            lines.push_back(std::string("Now use: ") + actionNames[action]);
        else if (action == actionCount)
            lines.push_back("Enter retries saving; Esc cancels.");
        std::ostringstream axes, buttons, hats;
        axes << "Axes:";
        for (int i = 0; i < SDL_JoystickNumAxes(joystick) && i < 6; ++i)
            axes << " " << i << "=" << SDL_JoystickGetAxis(joystick, i);
        buttons << "Buttons pressed:";
        for (int i = 0; i < SDL_JoystickNumButtons(joystick); ++i)
            if (SDL_JoystickGetButton(joystick, i))
                buttons << " " << i;
        hats << "Hats:";
        for (int i = 0; i < SDL_JoystickNumHats(joystick) && i < 4; ++i)
            hats << " " << i << "=" << static_cast<int>(SDL_JoystickGetHat(joystick, i));
        lines.push_back(axes.str());
        lines.push_back(buttons.str());
        lines.push_back(hats.str());
    }
    lines.push_back("");
    lines.push_back(status);
    return lines;
}
