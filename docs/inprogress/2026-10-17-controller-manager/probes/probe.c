#include <SDL3/SDL.h>
#include <stdio.h>
int main(){
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
  if(!SDL_Init(SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC)){printf("init fail %s\n",SDL_GetError());return 1;}
  for(int k=0;k<60;k++){SDL_Event e; while(SDL_PollEvent(&e)){} SDL_Delay(50);}
  int n=0; SDL_JoystickID*ids=SDL_GetJoysticks(&n); printf("joysticks %d\n",n);
  for(int i=0;i<n;i++){ SDL_JoystickID id=ids[i]; char g[64]; SDL_GUIDToString(SDL_GetJoystickGUIDForID(id),g,64);
    printf("[%u] name='%s' vid=%04x pid=%04x ver=%04x guid=%s path=%s isgp=%d type=%d\n",id,SDL_GetJoystickNameForID(id),SDL_GetJoystickVendorForID(id),SDL_GetJoystickProductForID(id),SDL_GetJoystickProductVersionForID(id),g,SDL_GetJoystickPathForID(id)?SDL_GetJoystickPathForID(id):"-",SDL_IsGamepad(id),SDL_GetGamepadTypeForID(id));
    if(SDL_IsGamepad(id)){ SDL_Gamepad*gp=SDL_OpenGamepad(id); char*m=SDL_GetGamepadMapping(gp); printf("  mapping=%s\n",m); SDL_free(m);
      SDL_Joystick*j=SDL_GetGamepadJoystick(gp); printf("  axes=%d buttons=%d hats=%d serial=%s fw=%04x conn=%d\n",SDL_GetNumJoystickAxes(j),SDL_GetNumJoystickButtons(j),SDL_GetNumJoystickHats(j),SDL_GetGamepadSerial(gp)?SDL_GetGamepadSerial(gp):"-",SDL_GetGamepadFirmwareVersion(gp),SDL_GetGamepadConnectionState(gp));
      for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;b++) if(SDL_GamepadHasButton(gp,b)) printf("  btn %s sf=%s\n",SDL_GetGamepadStringForButton(b),SDL_GetGamepadAppleSFSymbolsNameForButton(gp,b)?SDL_GetGamepadAppleSFSymbolsNameForButton(gp,b):"-");
      SDL_PropertiesID p=SDL_GetGamepadProperties(gp); printf("  rumble=%d trig=%d rgb=%d mono=%d player=%d\n",SDL_GetBooleanProperty(p,SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN,0),SDL_GetBooleanProperty(p,SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN,0),SDL_GetBooleanProperty(p,SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN,0),SDL_GetBooleanProperty(p,SDL_PROP_GAMEPAD_CAP_MONO_LED_BOOLEAN,0),SDL_GetBooleanProperty(p,SDL_PROP_GAMEPAD_CAP_PLAYER_LED_BOOLEAN,0));
      int pct; SDL_PowerState ps=SDL_GetGamepadPowerInfo(gp,&pct); printf("  power=%d pct=%d sensors accel=%d gyro=%d touchpads=%d\n",ps,pct,SDL_GamepadHasSensor(gp,SDL_SENSOR_ACCEL),SDL_GamepadHasSensor(gp,SDL_SENSOR_GYRO),SDL_GetNumGamepadTouchpads(gp));
    }}
  SDL_Quit(); return 0;}
