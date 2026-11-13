#pragma once
#include "state.h"
// Vista portable. No contiene lógica musical ni depende del DOM/Web Audio.
struct Display240View {
 const AppState* app;const char* chord;const char* timbre;bool active;bool menu;
 const char* title;const char* names[3];const char* values[3];int rows,selected;
 const float* scope;float peak,loopPhase;int loopState;uint32_t samples;
};
const uint16_t* display240Render(const Display240View& view,int theme);
