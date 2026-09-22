/* Non-simulation presentation and transport stubs for isolated route runs.
 * Timing, pathfinder, map, unit, pool and script VM use original object files. */
#include <stdio.h>
#include <stdarg.h>
#include "types.h"
#include "config.h"
#include "opendune.h"
#include "sprites.h"
#include "gui/gui.h"
#include "audio/audio.h"
#include "net/net.h"
#include "net/server.h"
#include "timer/timer.h"
#include "newui/actionpanel.h"

GameCfg g_gameConfig={.gameSpeed=2};
enum NetHostType g_host_type=HOSTTYPE_NONE;
uint16 g_activeAction,g_bloomSpriteID,g_builtSlabSpriteID,g_campaignID,g_landscapeSpriteID,g_scenarioID;
uint16 g_selectionHeight=1,g_selectionPosition,g_selectionType=4,g_selectionWidth=1;
int16 g_selectionState;
bool g_debugGame=false,g_debugScenario=false;
int g_factoryWindowTotal;
uint16 *g_iconMap;
uint16 g_validateStrictIfZero,g_veiledSpriteID,g_wallSpriteID;
int64_t g_timerGame,g_tickUnitBlinking,g_tickUnitDeviation,g_tickUnitMoveIndicator,g_tickUnitMovement;
int64_t g_tickUnitRotation,g_tickUnitScript,g_tickUnitUnknown4,g_tickUnitUnknown5;
uint16 Tools_AdjustToGameSpeed(uint16 normal,uint16 min,uint16 max,bool inverse) {return normal;}
int64_t Timer_GetTimer(enum TimerType timer){return g_timerGame;}
void Audio_PlaySample(enum SampleID id,int volume,float pan){}
void GUI_ChangeSelectionType(uint16 type){g_selectionType=type;}
void GUI_DisplayText(const char *str,int16 importance,...){}
void Server_Send_PlayBattleMusic(enum HouseFlag houses){}
void Server_Send_PlaySoundAtTile(enum HouseFlag houses,enum SoundID id,tile32 pos){}
void Server_Send_PlayVoice(enum HouseFlag houses,enum VoiceID id){}
void Server_Send_PlayVoiceAtTile(enum HouseFlag houses,enum VoiceID id,uint16 packed){}
void Server_Send_StatusMessage1(enum HouseFlag houses,uint8 priority,uint16 str1){}
void Server_Send_StatusMessage3(enum HouseFlag houses,uint8 priority,uint16 str1,uint16 str2,uint16 str3){}
void Warning(const char *fmt,...){va_list args;va_start(args,fmt);vfprintf(stderr,fmt,args);va_end(args);}
#include "newui/menubar.h"
void GUI_DisplayHint(enum HouseType house,enum StringID str,enum ShapeID shape){}
uint16 GUI_DisplayModalMessage(const char *str,uint16 shape,...){return 0;}
