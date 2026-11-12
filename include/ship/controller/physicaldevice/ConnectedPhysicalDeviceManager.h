#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <SDL2/SDL.h>

namespace Ship {

class ConnectedPhysicalDeviceManager {
  public:
    ConnectedPhysicalDeviceManager();
    ~ConnectedPhysicalDeviceManager();

    std::unordered_map<int32_t, SDL_GameController*> GetConnectedSDLGamepadsForPort(uint8_t portIndex);
    std::unordered_map<int32_t, std::string> GetConnectedSDLGamepadNames();
    std::unordered_set<int32_t> GetIgnoredInstanceIdsForPort(uint8_t portIndex);
    bool PortIsIgnoringInstanceId(uint8_t portIndex, int32_t instanceId);
    void IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId);
    void UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId);

    void HandlePhysicalDeviceConnect(int32_t sdlDeviceIndex);
    void HandlePhysicalDeviceDisconnect(int32_t sdlJoystickInstanceId);
    void RefreshConnectedSDLGamepads();

  private:
    void AutoAssignSDLGamepadsToPorts(const std::vector<int32_t>& instanceIdsInSDLOrder);
    void ResetAutoAssignedSDLGamepadPorts();

    std::unordered_map<int32_t, SDL_GameController*> mConnectedSDLGamepads;
    std::unordered_map<int32_t, std::string> mConnectedSDLGamepadNames;
    std::unordered_map<uint8_t, std::unordered_set<int32_t>> mIgnoredInstanceIds;

    // CVAR_CONTROLLER_AUTO_ASSIGN_PORTS state: instance id -> port index. Kept in memory only, never saved.
    std::unordered_map<int32_t, uint8_t> mAutoAssignedPorts;
    bool mAutoAssignActive = false;
};
} // namespace Ship
