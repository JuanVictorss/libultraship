#include "SDLRumbleMapping.h"

#include "public/bridge/consolevariablebridge.h"
#include "utils/StringHelper.h"
#include "Context.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>

namespace Ship {
#ifdef __ANDROID__
// SDL's Android joystick backend has no rumble, but Android exposes the controller's motor as a
// haptic device with the same name, so fall back to that.
static SDL_Haptic* AndroidHapticForGamepad(SDL_GameController* gamepad) {
    static std::unordered_map<std::string, SDL_Haptic*> sOpened;
    // Android names haptic devices after the InputDevice, which is the joystick name (not the mapping name)
    const char* padName = SDL_JoystickName(SDL_GameControllerGetJoystick(gamepad));
    if (padName == nullptr) {
        return nullptr;
    }
    auto it = sOpened.find(padName);
    if (it != sOpened.end()) {
        return it->second;
    }
    if (!SDL_WasInit(SDL_INIT_HAPTIC) && SDL_InitSubSystem(SDL_INIT_HAPTIC) != 0) {
        return nullptr;
    }
    SDL_Haptic* found = nullptr;
    for (int i = 0; i < SDL_NumHaptics() && found == nullptr; i++) {
        const char* hapticName = SDL_HapticName(i);
        if (hapticName != nullptr && strcmp(hapticName, padName) == 0) {
            found = SDL_HapticOpen(i);
            if (found != nullptr && SDL_HapticRumbleInit(found) != 0) {
                SDL_HapticClose(found);
                found = nullptr;
            }
        }
    }
    if (found != nullptr) {
        sOpened[padName] = found;
    }
    return found;
}
#endif

SDLRumbleMapping::SDLRumbleMapping(uint8_t portIndex, uint8_t lowFrequencyIntensityPercentage,
                                   uint8_t highFrequencyIntensityPercentage)
    : ControllerRumbleMapping(PhysicalDeviceType::SDLGamepad, portIndex, lowFrequencyIntensityPercentage,
                              highFrequencyIntensityPercentage) {
    SetLowFrequencyIntensity(lowFrequencyIntensityPercentage);
    SetHighFrequencyIntensity(highFrequencyIntensityPercentage);
}

void SDLRumbleMapping::StartRumble() {
    for (const auto& [instanceId, gamepad] :
         Context::GetInstance()->GetControlDeck()->GetConnectedPhysicalDeviceManager()->GetConnectedSDLGamepadsForPort(
             mPortIndex)) {
        if (SDL_GameControllerRumble(gamepad, mLowFrequencyIntensity, mHighFrequencyIntensity, 0) != 0) {
#ifdef __ANDROID__
            // The game pulses the motor on and off every few frames, so send short overlapping pulses
            // instead of start/stop pairs; vibration ends on its own shortly after the last start.
            static Uint32 sLastPulse = 0;
            Uint32 now = SDL_GetTicks();
            SDL_Haptic* haptic = AndroidHapticForGamepad(gamepad);
            if (haptic != nullptr && now - sLastPulse >= 60) {
                sLastPulse = now;
                float strength = std::max(mLowFrequencyIntensity, mHighFrequencyIntensity) / (float)UINT16_MAX;
                SDL_HapticRumblePlay(haptic, strength, 150);
            }
#endif
        }
    }
}

void SDLRumbleMapping::StopRumble() {
    for (const auto& [instanceId, gamepad] :
         Context::GetInstance()->GetControlDeck()->GetConnectedPhysicalDeviceManager()->GetConnectedSDLGamepadsForPort(
             mPortIndex)) {
        // On Android the short pulses from StartRumble stop by themselves
        SDL_GameControllerRumble(gamepad, 0, 0, 0);
    }
}

void SDLRumbleMapping::SetLowFrequencyIntensity(uint8_t intensityPercentage) {
    mLowFrequencyIntensityPercentage = intensityPercentage;
    mLowFrequencyIntensity = UINT16_MAX * (intensityPercentage / 100.0f);
}

void SDLRumbleMapping::SetHighFrequencyIntensity(uint8_t intensityPercentage) {
    mHighFrequencyIntensityPercentage = intensityPercentage;
    mHighFrequencyIntensity = UINT16_MAX * (intensityPercentage / 100.0f);
}

std::string SDLRumbleMapping::GetRumbleMappingId() {
    return StringHelper::Sprintf("P%d", mPortIndex);
}

void SDLRumbleMapping::SaveToConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".RumbleMappings." + GetRumbleMappingId();
    CVarSetString(StringHelper::Sprintf("%s.RumbleMappingClass", mappingCvarKey.c_str()).c_str(), "SDLRumbleMapping");
    CVarSetInteger(StringHelper::Sprintf("%s.LowFrequencyIntensity", mappingCvarKey.c_str()).c_str(),
                   mLowFrequencyIntensityPercentage);
    CVarSetInteger(StringHelper::Sprintf("%s.HighFrequencyIntensity", mappingCvarKey.c_str()).c_str(),
                   mHighFrequencyIntensityPercentage);
    CVarSave();
}

void SDLRumbleMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".RumbleMappings." + GetRumbleMappingId();

    CVarClear(StringHelper::Sprintf("%s.RumbleMappingClass", mappingCvarKey.c_str()).c_str());
    CVarClear(StringHelper::Sprintf("%s.LowFrequencyIntensity", mappingCvarKey.c_str()).c_str());
    CVarClear(StringHelper::Sprintf("%s.HighFrequencyIntensity", mappingCvarKey.c_str()).c_str());

    CVarSave();
}

std::string SDLRumbleMapping::GetPhysicalDeviceName() {
    return "SDL Gamepad";
}
} // namespace Ship
