#include <SDL3/SDL.h>
#include <stdlib.h>
#include <stdio.h>
int main(int argc,char**argv){ int secs=argc>1?atoi(argv[1]):120;
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1"); SDL_SetHint(SDL_HINT_JOYSTICK_MFI,"0");
  if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)){printf("init %s\n",SDL_GetError());return 1;}
  Uint64 end=SDL_GetTicks()+secs*1000ull; int lastAx[SDL_GAMEPAD_AXIS_COUNT]={0};
  while(SDL_GetTicks()<end){ SDL_Event e; while(SDL_PollEvent(&e)){
    if(e.type==SDL_EVENT_GAMEPAD_ADDED){ SDL_Gamepad*g=SDL_OpenGamepad(e.gdevice.which); printf("%8llu added '%s' %04x:%04x\n",SDL_GetTicks(),SDL_GetGamepadName(g),SDL_GetGamepadVendor(g),SDL_GetGamepadProduct(g)); }
    else if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN||e.type==SDL_EVENT_GAMEPAD_BUTTON_UP) printf("%8llu %s %s\n",SDL_GetTicks(),SDL_GetGamepadStringForButton(e.gbutton.button),e.gbutton.down?"down":"up");
    else if(e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION){ int a=e.gaxis.axis,q=e.gaxis.value/8192; if(q!=lastAx[a]){lastAx[a]=q; printf("%8llu axis %s %d\n",SDL_GetTicks(),SDL_GetGamepadStringForAxis(a),e.gaxis.value);} }
    fflush(stdout);} SDL_Delay(2);} SDL_Quit(); return 0; }
