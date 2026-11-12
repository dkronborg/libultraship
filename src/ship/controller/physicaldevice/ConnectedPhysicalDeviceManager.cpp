#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include <spdlog/spdlog.h>
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"

namespace Ship {
ConnectedPhysicalDeviceManager::ConnectedPhysicalDeviceManager() {
}

ConnectedPhysicalDeviceManager::~ConnectedPhysicalDeviceManager() {
}

std::unordered_map<int32_t, SDL_GameController*>
ConnectedPhysicalDeviceManager::GetConnectedSDLGamepadsForPort(uint8_t portIndex) {
    std::unordered_map<int32_t, SDL_GameController*> result;

    for (const auto& [instanceId, gamepad] : mConnectedSDLGamepads) {
        if (!PortIsIgnoringInstanceId(portIndex, instanceId)) {
            result[instanceId] = gamepad;
        }
    }

    return result;
}

std::unordered_map<int32_t, std::string> ConnectedPhysicalDeviceManager::GetConnectedSDLGamepadNames() {
    return mConnectedSDLGamepadNames;
}

std::unordered_set<int32_t> ConnectedPhysicalDeviceManager::GetIgnoredInstanceIdsForPort(uint8_t portIndex) {
    return mIgnoredInstanceIds[portIndex];
}

bool ConnectedPhysicalDeviceManager::PortIsIgnoringInstanceId(uint8_t portIndex, int32_t instanceId) {
    return GetIgnoredInstanceIdsForPort(portIndex).contains(instanceId);
}

void ConnectedPhysicalDeviceManager::IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {
    mIgnoredInstanceIds[portIndex].insert(instanceId);
}

void ConnectedPhysicalDeviceManager::UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {
    mIgnoredInstanceIds[portIndex].erase(instanceId);
}

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceConnect(int32_t sdlDeviceIndex) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceDisconnect(int32_t sdlJoystickInstanceId) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::RefreshConnectedSDLGamepads() {
    mConnectedSDLGamepads.clear();
    mConnectedSDLGamepadNames.clear();
    static SDL_JoystickGUID sZeroGuid;
    std::vector<int32_t> instanceIdsInSDLOrder;

    for (int32_t i = 0; i < SDL_NumJoysticks(); i++) {

        SDL_JoystickGUID deviceGUID = SDL_JoystickGetDeviceGUID(i);
        if (SDL_memcmp(&deviceGUID, &sZeroGuid, sizeof(deviceGUID)) == 0) {
            SPDLOG_WARN(
                "Calling SDL JoystickGetDeviceGUID with index ({:d}) returned zero GUID. This is likely due to an "
                "invalid index. Refer to https://wiki.libsdl.org/SDL2/SDL_JoystickGetDeviceGUID for more information.",
                i);
            continue;
        }

        char deviceGuidCStr[33] = "";
        SDL_JoystickGetGUIDString(deviceGUID, deviceGuidCStr, sizeof(deviceGuidCStr));

        if (!SDL_IsGameController(i)) {
            SPDLOG_WARN("SDL Joystick (GUID: {}) not recognized as gamepad."
                        "This is likely due to a missing mapping string in gamecontrollerdb.txt."
                        "Refer to https://github.com/mdqinc/SDL_GameControllerDB for more information.",
                        deviceGuidCStr);
            continue;
        }

        auto gamepad = SDL_GameControllerOpen(i);
        if (gamepad == nullptr) {
            SPDLOG_ERROR("SDL GameControllerOpen error (GUID: {}): {}", deviceGuidCStr, SDL_GetError());
            continue;
        }

        auto instanceId = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad));
        if (instanceId < 0) {
            SPDLOG_ERROR("SDL JoystickInstanceID error (GUID: {}): {}", deviceGuidCStr, SDL_GetError());
            continue;
        }

        std::string gamepadName;
        auto name = SDL_GameControllerName(gamepad);
        if (name == nullptr) {
            gamepadName = deviceGuidCStr;
            SPDLOG_WARN("SDL_GameControllerName returned null. Setting name to GUID \"{}\" instead.", gamepadName);
        } else {
            gamepadName = name;
        }

        mConnectedSDLGamepads[instanceId] = gamepad;
        mConnectedSDLGamepadNames[instanceId] = gamepadName;
        instanceIdsInSDLOrder.push_back(instanceId);

        for (uint8_t port = 1; port < 4; port++) {
            mIgnoredInstanceIds[port].insert(instanceId);
        }
    }

    if (Context::GetInstance()->GetConsoleVariables()->GetInteger(CVAR_CONTROLLER_AUTO_ASSIGN_PORTS, 0)) {
        AutoAssignSDLGamepadsToPorts(instanceIdsInSDLOrder);
    } else if (mAutoAssignActive) {
        ResetAutoAssignedSDLGamepadPorts();
    }
}

// Opt-in: give each SDL gamepad exactly one port, in connection (SDL enumeration) order.
// Ports are sticky: a gamepad keeps its port while it stays connected, and a disconnect frees that port without
// moving anyone else. Gamepads without a port (newly connected ones, or extras beyond 4) take the lowest free
// port; if none is free they are ignored on every port until one frees up.
void ConnectedPhysicalDeviceManager::AutoAssignSDLGamepadsToPorts(const std::vector<int32_t>& instanceIdsInSDLOrder) {
    mAutoAssignActive = true;

    std::erase_if(mAutoAssignedPorts,
                  [this](const auto& entry) { return !mConnectedSDLGamepads.contains(entry.first); });

    bool portTaken[4] = {};
    for (const auto& [instanceId, port] : mAutoAssignedPorts) {
        portTaken[port] = true;
    }

    for (auto instanceId : instanceIdsInSDLOrder) {
        if (mAutoAssignedPorts.contains(instanceId)) {
            continue;
        }
        for (uint8_t port = 0; port < 4; port++) {
            if (!portTaken[port]) {
                mAutoAssignedPorts[instanceId] = port;
                portTaken[port] = true;
                SPDLOG_INFO("Auto-assigned SDL gamepad \"{}\" (instance id {}) to port {}",
                            mConnectedSDLGamepadNames[instanceId], instanceId, port + 1);
                break;
            }
        }
    }

    for (auto instanceId : instanceIdsInSDLOrder) {
        auto assigned = mAutoAssignedPorts.find(instanceId);
        for (uint8_t port = 0; port < 4; port++) {
            if (assigned != mAutoAssignedPorts.end() && assigned->second == port) {
                UnignoreInstanceIdForPort(port, instanceId);
            } else {
                IgnoreInstanceIdForPort(port, instanceId);
            }
        }
    }
}

// Auto-assign was just turned off: go back to the default of every gamepad on port 1 only.
// Ports 2-4 were already set to ignore every gamepad by RefreshConnectedSDLGamepads.
void ConnectedPhysicalDeviceManager::ResetAutoAssignedSDLGamepadPorts() {
    mAutoAssignActive = false;
    mAutoAssignedPorts.clear();

    for (const auto& [instanceId, gamepad] : mConnectedSDLGamepads) {
        UnignoreInstanceIdForPort(0, instanceId);
    }
}
} // namespace Ship
