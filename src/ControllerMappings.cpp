#include "ControllerMappings.h"

SDL_GameController* OpenControllerWithCurrentMapping(int deviceIndex, SDL_GameController* current) {
    if (current) {
        char* activeMapping = SDL_GameControllerMapping(current);
        char* deviceMapping = SDL_GameControllerMappingForDeviceIndex(deviceIndex);
        const bool unchanged = activeMapping && deviceMapping && SDL_strcmp(activeMapping, deviceMapping) == 0;
        SDL_free(activeMapping);
        SDL_free(deviceMapping);
        if (unchanged)
            return current;
        SDL_GameControllerClose(current);
    }
    return SDL_GameControllerOpen(deviceIndex);
}
