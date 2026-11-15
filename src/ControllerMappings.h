#ifndef CONTROLLER_MAPPINGS_H
#define CONTROLLER_MAPPINGS_H

#include <SDL.h>

// Takes ownership of current. Reopens it when SDL now selects a different
// mapping (notably when replacing a generic mapping with a device-specific one).
// Returns NULL on failure, with the reason in SDL_GetError().
SDL_GameController* OpenControllerWithCurrentMapping(int deviceIndex, SDL_GameController* current);

#endif
