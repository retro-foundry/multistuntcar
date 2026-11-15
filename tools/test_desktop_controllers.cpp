#include "ControllerMappings.h"
#include "DesktopControllerConfig.h"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message + ": " + SDL_GetError());
}

void InitializeSDL() {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    Require(SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0, "SDL initialization");
}

void Pump(DesktopControllerConfig& config) {
    SDL_JoystickUpdate();
    SDL_Event event;
    while (SDL_PollEvent(&event))
        config.HandleEvent(event);
}

void Key(DesktopControllerConfig& config, SDL_Keycode key) {
    SDL_Event event = {};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    config.HandleEvent(event);
}

struct Device {
    SDL_Joystick* joystick;
    SDL_JoystickID instance;

    explicit Device(Uint16 product) {
        SDL_VirtualJoystickDesc description = {};
        description.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        description.type = SDL_JOYSTICK_TYPE_FLIGHT_STICK; // Deliberately has no game-controller mapping.
        description.naxes = 2;
        description.nbuttons = 5;
        description.nhats = 1;
        description.vendor_id = 0x2341;
        description.product_id = product;
        description.name = "USB retro joystick regression device";
        const int index = SDL_JoystickAttachVirtualEx(&description);
        Require(index >= 0, "attach virtual joystick");
        joystick = SDL_JoystickOpen(index);
        Require(joystick != nullptr, "open virtual joystick");
        instance = SDL_JoystickInstanceID(joystick);
    }

    int Index() const {
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_JoystickGetDeviceInstanceID(i) == instance)
                return i;
        return -1;
    }

    void Detach() {
        if (Index() >= 0)
            Require(SDL_JoystickDetachVirtual(Index()) == 0, "detach virtual joystick");
    }

    ~Device() {
        Detach();
        SDL_JoystickClose(joystick);
    }

    void Axis(DesktopControllerConfig& config, int axis, Sint16 value) {
        Require(SDL_JoystickSetVirtualAxis(joystick, axis, value) == 0, "set virtual axis");
        Pump(config);
    }

    void Button(DesktopControllerConfig& config, int button, Uint8 value) {
        Require(SDL_JoystickSetVirtualButton(joystick, button, value) == 0, "set virtual button");
        Pump(config);
    }

    void Hat(DesktopControllerConfig& config, Uint8 value) {
        Require(SDL_JoystickSetVirtualHat(joystick, 0, value) == 0, "set virtual hat");
        Pump(config);
    }
};

void Select(DesktopControllerConfig& config, const Device& device) {
    if (!config.IsOpen())
        config.Toggle();
    Pump(config);
    for (int i = 0; i < SDL_NumJoysticks() && config.SelectedInstance() != device.instance; ++i)
        Key(config, SDLK_DOWN);
    Require(config.SelectedInstance() == device.instance, "select the intended joystick");
}

void BindAxes(DesktopControllerConfig& config, Device& device) {
    Key(config, SDLK_RETURN);
    device.Axis(config, 0, -32767);
    device.Axis(config, 0, 0);
    device.Axis(config, 0, 32767);
    device.Axis(config, 0, 0);
    device.Axis(config, 1, -32767);
    device.Axis(config, 1, 0);
    device.Axis(config, 1, 32767);
    device.Axis(config, 1, 0);
    device.Button(config, 0, SDL_PRESSED);
    device.Button(config, 0, SDL_RELEASED);
}

SDL_GameController* OpenController(const Device& device) {
    Require(SDL_IsGameController(device.Index()) == SDL_TRUE, "configured adapter is now a game controller");
    SDL_GameController* controller = OpenControllerWithCurrentMapping(device.Index(), nullptr);
    Require(controller != nullptr, "open configured game controller");
    return controller;
}

void CheckAxes(DesktopControllerConfig& config, Device& device, SDL_GameController* controller) {
    const SDL_GameControllerButton outputs[] = {SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
                                                SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN};
    for (int i = 0; i < 4; ++i) {
        const int axis = i / 2;
        device.Axis(config, axis, i % 2 == 0 ? -32767 : 32767);
        Require(SDL_GameControllerGetButton(controller, outputs[i]) == 1, "mapped joystick direction");
        device.Axis(config, axis, 0);
        Require(SDL_GameControllerGetButton(controller, outputs[i]) == 0, "released joystick direction");
    }
    device.Button(config, 0, SDL_PRESSED);
    Require(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) == 1, "fire maps to boost/menu confirm");
    device.Button(config, 0, SDL_RELEASED);
    Require(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) == 0, "fire release");
}

void TestAxes(const std::string& path) {
    DesktopControllerConfig config;
    Require(config.Initialize(path.c_str()), "initialize controller preferences");
    Device device(0xf001);
    Device other(0xf002);
    Require(SDL_IsGameController(device.Index()) == SDL_FALSE, "test adapter starts unmapped");
    Select(config, device);
    Key(config, SDLK_RETURN);
    device.Axis(config, 0, 10000); // Noise must not advance the wizard.
    device.Axis(config, 0, 0);
    other.Axis(config, 0, 32767); // A different controller must not supply a binding.
    Key(config, SDLK_ESCAPE);     // Cancel without applying or saving anything.
    Require(config.Revision() == 0 && SDL_IsGameController(device.Index()) == SDL_FALSE, "cancel keeps old mapping");
    device.Axis(config, 0, -32767); // A held control must be released before it can be learned.
    Key(config, SDLK_RETURN);
    device.Axis(config, 0, -20000);
    device.Axis(config, 0, 0);
    device.Axis(config, 0, -32767);
    device.Axis(config, 0, 0);
    device.Axis(config, 0, 32767);
    device.Axis(config, 0, 0);
    device.Axis(config, 1, -32767);
    device.Axis(config, 1, 0);
    device.Axis(config, 1, 32767);
    device.Axis(config, 1, 0);
    device.Button(config, 0, SDL_PRESSED);
    device.Button(config, 0, SDL_RELEASED);
    Require(config.Revision() == 1, "complete axis bindings are saved once");
    SDL_GameController* controller = OpenController(device);
    CheckAxes(config, device, controller);
    Require(OpenControllerWithCurrentMapping(device.Index(), controller) == controller,
            "unchanged mapping keeps handle");
    SDL_GameControllerClose(controller);
    config.Close();
    std::cout << "PASS: generic adapter, all axes/fire, cancellation, held input, device selection\n";
}

void TestReload(const std::string& path) {
    // All SDL mapping state is gone. Only the saved file can restore this adapter.
    Device device(0xf001);
    Require(SDL_IsGameController(device.Index()) == SDL_FALSE, "fresh SDL session starts without the custom mapping");
    DesktopControllerConfig config;
    Require(config.Initialize(path.c_str()), "reload saved controller preferences");
    SDL_GameController* controller = OpenController(device);
    CheckAxes(config, device, controller);
    SDL_GameControllerClose(controller);
    std::cout << "PASS: saved mappings survive an SDL restart\n";
}

void TestHatsAndButtons(const std::string& path) {
    DesktopControllerConfig config;
    Require(config.Initialize(path.c_str()), "initialize hats/buttons preferences");
    Device device(0xf003);
    Select(config, device);
    Key(config, SDLK_RETURN);
    const Uint8 hats[] = {SDL_HAT_LEFT, SDL_HAT_RIGHT, SDL_HAT_UP, SDL_HAT_DOWN};
    for (Uint8 hat : hats) {
        device.Hat(config, hat);
        device.Hat(config, SDL_HAT_CENTERED);
    }
    device.Button(config, 0, SDL_PRESSED);
    device.Button(config, 0, SDL_RELEASED);
    Require(config.Revision() == 1, "hat binding saved");
    SDL_GameController* controller = OpenController(device);
    device.Hat(config, SDL_HAT_LEFTUP);
    Require(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT) &&
                SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP),
            "diagonal hat activates both configured directions");
    device.Hat(config, SDL_HAT_CENTERED);

    Key(config, SDLK_RETURN);
    for (int button = 4; button >= 0; --button) {
        device.Button(config, button, SDL_PRESSED);
        device.Button(config, button, SDL_RELEASED);
    }
    Require(config.Revision() == 2, "button-only reconfiguration saved");
    controller = OpenControllerWithCurrentMapping(device.Index(), controller);
    Require(controller != nullptr, "refresh button-only mapping");
    const SDL_GameControllerButton outputs[] = {SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
                                                SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                                                SDL_CONTROLLER_BUTTON_A};
    for (int i = 0; i < 5; ++i) {
        device.Button(config, 4 - i, SDL_PRESSED);
        Require(SDL_GameControllerGetButton(controller, outputs[i]) == 1, "button-only directional mapping");
        device.Button(config, 4 - i, SDL_RELEASED);
    }
    SDL_GameControllerClose(controller);
    config.Close();
    std::cout << "PASS: hat directions/diagonals and button-only reconfiguration\n";
}

void TestDisconnectAndSaveFailure(const std::string& path) {
    DesktopControllerConfig config;
    Require(config.Initialize(path.c_str()), "initialize disconnect preferences");
    Device device(0xf004);
    Select(config, device);
    Key(config, SDLK_RETURN);
    device.Axis(config, 0, -32767);
    device.Detach();
    Pump(config);
    Require(config.Revision() == 0 && config.SelectedInstance() != device.instance, "disconnect cancels capture");
    config.Close();

    DesktopControllerConfig unwritable;
    Require(!unwritable.Initialize(("missing-" + path + "/controllers.txt").c_str()),
            "unwritable preferences reported");
    Device second(0xf005);
    Select(unwritable, second);
    BindAxes(unwritable, second);
    Require(unwritable.Revision() == 0 && SDL_IsGameController(second.Index()) == SDL_FALSE,
            "failed save does not apply an unsaved mapping");
    bool errorVisible = false;
    for (const std::string& line : unwritable.Lines())
        errorVisible = errorVisible || line.find("Cannot save") != std::string::npos;
    Require(errorVisible, "save error remains visible in setup");
    unwritable.Close();
    std::cout << "PASS: disconnect cancellation and visible persistence failures\n";
}

void TestGenericMappingUpgrade(const std::string& path) {
    Require(SDL_GameControllerAddMapping("default,Generic test gamepad,a:b0,leftx:a0,lefty:a1,") >= 0,
            "install generic mapping");
    Device device(0xf006);
    SDL_GameController* controller = OpenController(device);
    Require(SDL_strcmp(SDL_GameControllerName(controller), "Generic test gamepad") == 0, "generic mapping is active");
    DesktopControllerConfig config;
    Require(config.Initialize(path.c_str()), "initialize mapping upgrade preferences");
    Select(config, device);
    BindAxes(config, device);
    Require(config.Revision() == 1, "new device-specific mapping saved");
    controller = OpenControllerWithCurrentMapping(device.Index(), controller);
    Require(controller && SDL_strcmp(SDL_GameControllerName(controller), "Custom desktop joystick") == 0,
            "already open generic controller immediately uses the saved mapping");
    CheckAxes(config, device, controller);
    SDL_GameControllerClose(controller);
    config.Close();
    std::cout << "PASS: existing generic controller switches mappings immediately\n";
}
} // namespace

int main() {
    const std::string path = "controller-regression-" + std::to_string(SDL_GetPerformanceCounter()) + ".txt";
    try {
        InitializeSDL();
        TestAxes(path);
        SDL_Quit();
        InitializeSDL();
        TestReload(path);
        TestHatsAndButtons(path);
        TestDisconnectAndSaveFailure(path);
        TestGenericMappingUpgrade(path);
        SDL_Quit();
        std::remove(path.c_str());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        SDL_Quit();
        std::remove(path.c_str());
        return 1;
    }
}
