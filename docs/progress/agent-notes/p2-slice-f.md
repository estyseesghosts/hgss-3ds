# P2 Slice F: the game build harness (host32 + 3DS static link)

Date: 2026-10-31. Dev container (Ubuntu 24.04, gcc-10, devkitARM 16.1.0).
Repo at `0b81803`. The lead was re-bootstrapping `build/upstream` during this
slice, so it moved (observed: `5450a13` -> `20530b8` -> `7f8bc46`). The
measurements below are from stable runs at `20530b8`; rerun both commands
after the patch stack settles before trusting the numbers.

## What was added

- `tools/hgss_game.py` - the harness. It runs `prepare_game_headers.py` into
  `build/upstream`, compiles every `src/**/*.c` and `src/**/*.cpp` from
  `game_sources.py` plus the game libs neither archive provides
  (`lib/src` NitroCrypto, `lib/syscall/src`), assembles all 150 ARM9 `.s`
  through `s_to_gas.py`, builds `libnitro.a`/`libnitrosystem.a` through
  `hgss_nitro.py`, groups overlay sections and writes `overlay_table.c` +
  the overlay-ID ld script, generates the Wi-Fi stand-ins with
  `make_game_wifi_stubs.py`, weakens duplicate symbols (the Platinum
  Makefile's census), and links `pokeheartgold-pc`.
- `hgss/game/app/heartgold.{c,h}` - the HGSS counterpart of
  `3ds_port/game/app/platinum.c`: firmware header, overlay table,
  save path, ARM7 services, `NitroMain()`.
- `hgss/game/app/main_pc.c` - a minimal PC shell (`main`, the four port
  hooks, `--save`/`--frames`). Deliberately not Platinum's 88 KB runner.
- Root `Makefile`: `game-pc` and `game-3ds` (the only change to the file).
- `tests/test_hgss_game.py`: 21 synthetic tests for source selection,
  flags/includes, command construction, link order and output parsing.
- `hgss/game/stubs/wm_stubs.c`: added `WMi_StartParentEx`. The game's data
  tables (`lib/asm/nitro_sdk_data.s`, `lib/NitroSDK/asm/mb_data.s`) name
  it, so without it the linker pulls the real `wm_requests.o`/`wm_core.o`
  and collides with every `WM_*` stand-in.

## Commands

```bash
python3 tools/hgss_game.py --target host32 --keep-going --out /work/out/game1
python3 tools/hgss_game.py --target 3ds    --keep-going --out /work/out/game3ds
# or: make game-pc / make game-3ds (outputs under build/game-{pc,3ds})
```

## host32 result

- **50 of 1094 C/C++ files failed** (424 compiler errors); 1044 compiled. 1012 game C + 56 C++ + 26 game-lib C were attempted.
- **150/150 ARM9 `.s` assembled** with `s_to_gas.py` (0 errors).
- `libnitro.a` built (4.2 MB). `libnitrosystem.a` did **not**: the vendored
  nitrosystem build fails in its generated `nns_all_headers.c` unity file
  (`NNSSndHandle` unknown; `SND_RecvCommandReply`, `SND_ReadDriverInfo`,
  `SND_SetMasterVolume`, `SND_PrepareSeq` conflict with
  `hgss/compat/include/nitro/snd/ARM9/interface.h`). The harness links with
  the archives that exist and reports the rest as unresolved.
- Overlay table: 129 overlays, 793 of 913 overlay objects present (120 not
  built), 50 with data, 0 with static initializers.
- Duplicate-symbol census: 2 names in more than one object
  (`FrontierScript_ReadByte`, `sListMenuTemplate`); 12 objects weakened.
- Wi-Fi stand-ins: 186 functions.
- **Link: 966 unique unresolved symbols.** Full list below.

### host32 compile failures (count, file, log)

```
  12  lib/syscall/src/_secure_IPGE.c  (logs/obj/lib/syscall/src/_secure_IPGE.o.log)
  11  lib/syscall/src/_secure_IPKE.c  (logs/obj/lib/syscall/src/_secure_IPKE.o.log)
 165  src/application/ball_capsule_editor.c  (logs/obj/src/application/ball_capsule_editor.o.log)
   1  src/application/main_menu/agb_cartridge.c  (logs/obj/src/application/main_menu/agb_cartridge.o.log)
   3  src/application/pokewalker/pokewalker_ir.c  (logs/obj/src/application/pokewalker/pokewalker_ir.o.log)
   1  src/application/pokewalker/pokewalker_link.c  (logs/obj/src/application/pokewalker/pokewalker_link.o.log)
   8  src/battle/battle_command.c  (logs/obj/src/battle/battle_command.o.log)
   1  src/battle/battle_hp_bar.c  (logs/obj/src/battle/battle_hp_bar.o.log)
  17  src/battle/battle_main.c  (logs/obj/src/battle/battle_main.o.log)
   5  src/battle/party_gauge.c  (logs/obj/src/battle/party_gauge.o.log)
   3  src/comm_state.c  (logs/obj/src/comm_state.o.log)
   1  src/field/event_cutscene_celebi.c  (logs/obj/src/field/event_cutscene_celebi.o.log)
   1  src/field/event_cutscene_shaymin.c  (logs/obj/src/field/event_cutscene_shaymin.o.log)
  14  src/field/weather_manager.c  (logs/obj/src/field/weather_manager.o.log)
   2  src/field2d/field2d_view.c  (logs/obj/src/field2d/field2d_view.o.log)
   1  src/field_take_photo.c  (logs/obj/src/field_take_photo.o.log)
   1  src/game_stats_request.c  (logs/obj/src/game_stats_request.o.log)
   9  src/launch_application.c  (logs/obj/src/launch_application.o.log)
   1  src/lobby/lobby_tree_nodes.c  (logs/obj/src/lobby/lobby_tree_nodes.o.log)
   1  src/map_preview_graphic.c  (logs/obj/src/map_preview_graphic.o.log)
  57  src/nintendo_wfc_connect_unused_session.c  (logs/obj/src/nintendo_wfc_connect_unused_session.o.log)
   3  src/overlay_03/pokeathlon_records.c  (logs/obj/src/overlay_03/pokeathlon_records.o.log)
   1  src/pokeathlon/pokeathlon_box.c  (logs/obj/src/pokeathlon/pokeathlon_box.o.log)
   1  src/pokeathlon/pokeathlon_box_graphics.c  (logs/obj/src/pokeathlon/pokeathlon_box_graphics.o.log)
   1  src/render_text.c  (logs/obj/src/render_text.o.log)
   2  src/save_fashion.c  (logs/obj/src/save_fashion.o.log)
   1  src/scrcmd_link_sync.c  (logs/obj/src/scrcmd_link_sync.o.log)
   1  src/screen_fade_effects.c  (logs/obj/src/screen_fade_effects.o.log)
   4  src/sound_gb.c  (logs/obj/src/sound_gb.o.log)
   4  src/sound_playback.c  (logs/obj/src/sound_playback.o.log)
   4  src/sound_system.c  (logs/obj/src/sound_system.o.log)
   4  src/voice_chat.c  (logs/obj/src/voice_chat.o.log)
   4  src/voice_chat_conference.c  (logs/obj/src/voice_chat_conference.o.log)
   4  src/wifi_club/wifi_club_app.c  (logs/obj/src/wifi_club/wifi_club_app.o.log)
   4  src/wifi_connection.c  (logs/obj/src/wifi_connection.o.log)
   3  src/wifi_plaza/wifi_plaza_history.c  (logs/obj/src/wifi_plaza/wifi_plaza_history.o.log)
  33  src/wifi_plaza/wifi_plaza_news.c  (logs/obj/src/wifi_plaza/wifi_plaza_news.o.log)
   1  src/wifi_plaza/wifi_plaza_time_zone.c  (logs/obj/src/wifi_plaza/wifi_plaza_time_zone.o.log)
   4  src/wifi_setup/wifi_setup_access_points.c  (logs/obj/src/wifi_setup/wifi_setup_access_points.o.log)
   1  src/wifi_setup/wifi_setup_app.c  (logs/obj/src/wifi_setup/wifi_setup_app.o.log)
   4  src/wifi_setup/wifi_setup_local_wireless.c  (logs/obj/src/wifi_setup/wifi_setup_local_wireless.o.log)
   1  src/lobby/lobby_channel_map_insert_node.cpp  (logs/obj/src/lobby/lobby_channel_map_insert_node.o.log)
   1  src/lobby/lobby_chat_split_string.cpp  (logs/obj/src/lobby/lobby_chat_split_string.o.log)
   1  src/lobby/lobby_event_deque_capacity.cpp  (logs/obj/src/lobby/lobby_event_deque_capacity.o.log)
   1  src/lobby/lobby_user_id_set_copy_nodes.cpp  (logs/obj/src/lobby/lobby_user_id_set_copy_nodes.o.log)
   8  src/lobby/ppw_lobby.cpp  (logs/obj/src/lobby/ppw_lobby.o.log)
   1  src/lobby/ppw_lobby_players_update_states.cpp  (logs/obj/src/lobby/ppw_lobby_players_update_states.o.log)
   1  src/lobby/ppw_lobby_process.cpp  (logs/obj/src/lobby/ppw_lobby_process.o.log)
   6  src/to91_msl_array_new.cpp  (logs/obj/src/to91_msl_array_new.o.log)
   5  src/to91_msl_scalar_new.cpp  (logs/obj/src/to91_msl_scalar_new.o.log)
```

The large ones are the known GCC-vs-MWCC patches still to write:
`ball_capsule_editor.c` (165: the SDK's `NitroStaticInit_` macro does not
expand as MWCC's did), `nintendo_wfc_connect_unused_session.c` (57),
`_secure_IPGE/_IPKE.c` (23: syscall asm), `battle_main.c` (17),
`weather_manager.c` (14). `comm_state.c` (3) is the report's old 80-error
item after other patches landed: `MATHRandContext32` redeclaration, a
`global.h` const-cast, and a label before a declaration. The rest are one
to nine errors each; every failure has a log under `/work/out/game1/logs/`.

## 3ds result

- **76 of 1094 C/C++ failed** (511 errors); 1018 compiled. 150/150 `.s`
  assembled. The 3DS build compiles 26 fewer files than host32: devkitARM 16
  turns some GCC-10 diagnostics into errors (and vice versa).
- Neither `libnitro.a` nor `libnitrosystem.a` built: the ARM7 sound rule
  fails assembling `hgss_compat/ds_sound_tables.c` (`junk at end of line`,
  `unrecognized symbol type ""` - the N7_ symbol-renaming step), and
  nitrosystem fails at the same `nns_all_headers.c` unity file.
- **Static link attempt: 1555 unique unresolved symbols** (no archives).
  This is a symbol dump, not a meaningful 3DS link yet; the 3DS shell
  (`main_3ds.c`, libctru, `3dsx.specs`) belongs to Phase 5.

### 3ds compile failures (count, file)

```
  12  lib/syscall/src/_secure_IPGE.c
  11  lib/syscall/src/_secure_IPKE.c
 169  src/application/ball_capsule_editor.c
   8  src/application/dress_up/dress_up.c
   1  src/application/main_menu/agb_cartridge.c
   1  src/application/palpad/palpad_friend_code.c
   4  src/application/pc_box/pc_box_ui.c
  10  src/application/plaza_game_mime_jr_top.c
   6  src/application/plaza_game_swalot_plop.c
   1  src/application/pokedex/pokedex_main_seq.c
   3  src/application/pokewalker/pokewalker_ir.c
   1  src/application/pokewalker/pokewalker_link.c
   2  src/application/wobbuffet_pop_scene.c
   7  src/battle/battle_command.c
   1  src/battle/battle_controller_player.c
   1  src/battle/battle_hp_bar.c
  21  src/battle/battle_main.c
   2  src/comm_state.c
   1  src/communication_player_state.c
   1  src/field/event_cutscene_celebi.c
   1  src/field/event_cutscene_shaymin.c
   2  src/field/event_cutscene_sinjoh.c
   1  src/field/fieldmap.c
   1  src/field/map_object_models.c
   2  src/field/scrcmd_pokemon_misc.c
   2  src/field_move.c
   1  src/frontier/battle_hall_data.c
   1  src/frontier/frontier_cmd_arcade.c
   1  src/game_stats_request.c
   1  src/launch_application.c
   1  src/lobby/lobby_tree_nodes.c
   2  src/map_object_behavior.c
   1  src/nintendo_wfc_connect.c
  57  src/nintendo_wfc_connect_unused_session.c
   1  src/pokeathlon/pokeathlon_box_graphics.c
   2  src/pokeathlon/pokeathlon_circle_push.c
   8  src/pokeathlon/pokeathlon_circle_push_field.c
   6  src/pokeathlon/pokeathlon_disc_catch.c
   2  src/pokeathlon/pokeathlon_disc_catch_field.c
   4  src/pokeathlon/pokeathlon_goal_roll.c
   1  src/pokeathlon/pokeathlon_goal_roll_field.c
   2  src/pokeathlon/pokeathlon_hurdle_dash_race.c
   9  src/pokeathlon/pokeathlon_lamp_jump.c
   2  src/pokeathlon/pokeathlon_pennant_capture.c
   6  src/pokeathlon/pokeathlon_pennant_capture_cpu.c
   4  src/pokeathlon/pokeathlon_pennant_capture_field.c
   2  src/pokeathlon/pokeathlon_pennant_capture_play.c
   8  src/pokeathlon/pokeathlon_relay_run.c
   6  src/pokeathlon/pokeathlon_relay_run_cpu.c
   2  src/pokeathlon/pokeathlon_ring_drop_field.c
   2  src/pokeathlon/pokeathlon_ring_drop_mon.c
   6  src/pokepic_anim.c
   2  src/save_fashion.c
   1  src/screen_fade_effects.c
   2  src/screen_window_wipe.c
   4  src/sound_gb.c
   4  src/sound_playback.c
   4  src/sound_system.c
   4  src/voice_chat.c
   4  src/voice_chat_conference.c
   5  src/wifi_connection.c
   3  src/wifi_plaza/wifi_plaza_history.c
  33  src/wifi_plaza/wifi_plaza_news.c
   1  src/wifi_plaza/wifi_plaza_time_zone.c
   4  src/wifi_setup/wifi_setup_access_points.c
   1  src/wifi_setup/wifi_setup_app.c
   4  src/wifi_setup/wifi_setup_local_wireless.c
   1  src/lobby/lobby_channel_map_insert_node.cpp
   1  src/lobby/lobby_chat_split_string.cpp
   1  src/lobby/lobby_event_deque_capacity.cpp
   1  src/lobby/lobby_user_id_set_copy_nodes.cpp
   8  src/lobby/ppw_lobby.cpp
   1  src/lobby/ppw_lobby_players_update_states.cpp
   1  src/lobby/ppw_lobby_process.cpp
   6  src/to91_msl_array_new.cpp
   5  src/to91_msl_scalar_new.cpp
```

## host32 unresolved symbols (966)

```
TextFlags_SetCanTouchSpeedUpPrint
Sound_SetSceneAndPlayBGM
Sound_ClearSecondaryScene
PlaySE
SoundSys_GetGBSoundsState
SoundSys_ToggleGBSounds
NNS_G3dGlbMaterialColorDiffAmb
NNS_G3dGlbMaterialColorSpecEmi
NNS_G3dGlbPolygonAttr
NNS_G3dGlbLightVector
NNS_G3dGlbLightColor
NNS_G3dGetTex
NNSi_G3dModifyMatFlag
NNSi_G3dModifyPolygonAttrMask
NNS_G3dGetAnmByIdx
NNS_G3dAllocAnmObj
NNS_G3dAnmObjInit
NNS_G3dRenderObjAddAnmObj
NNS_G3dFreeAnmObj
NNS_G3dRenderObjRemoveAnmObj
NNS_G2dGetUnpackedBGCharacterData
TextFlags_SetCanABSpeedUpPrint
TextFlags_SetAutoScrollParam
GF_IsAnySEPlaying
IsFanfarePlaying
IsSEPlaying
NNS_G2dGetImageLocation
StopBGM
Sound_SetBGMPlayerChannels
Sound_SetScene
CommState_SetMaxEntries
NNS_G3dGlb
NNS_G2dGetUnpackedCharacterData
NNS_G3dGlbSetBaseTrans
NNS_G3dGlbSetBaseScale
NNS_G3dGeBufferOP_N
NNS_G3dTexLoad
NNS_G3dPlttLoad
NNS_G3dGetMdlSet
NNS_G3dTexReleaseTexKey
NNS_G3dPlttReleasePlttKey
NNS_G3dTexGetRequiredSize
NNS_G3dPlttGetRequiredSize
NNS_G3dTexSetTexKey
NNS_G3dPlttSetPlttKey
_ll_mul
NNS_G2dGetUnpackedScreenData
CommState_Exists
CommState_StopBeaconWatch
PmAgbCartridgeGetOffsets
PmAgbCartridge_GetVersionInternal
PmAgbCartridge_GetLanguage
CommState_StartBeaconWatch
CommState_GetSeenGames
Sound_Stop
NNS_G2dInitOamManagerModule
NNS_G2dGetImagePaletteLocation
PmAgbCartridge_GetTrainerId
PmAgbCartridge_StartSave
PmAgbCartridge_GetSaveStatus
PmAgbCartridge_ContinueSave
MigrateBoxMon
AGB_SetBoxMonData
AGB_GetBoxMonData
TranslateAgbSpecies
AGB_GetBoxMonLevel
PlayCry
PmAgbCartridge_GetPlayerName
PmAgbCartridgeUnlinkSpec
PmAgbCartridge_LoadSave
PmAgbCartridge_GetPCStorage
CommState_SetErrorWatch
WifiConnection_ClassifyErrorCode
CommState_FreeMysteryGiftWfcSession
Comm_InitDwc
CommState_InitMysteryGiftWfcSession
CommState_ConnectToSlot
CommState_IsLinked
CommState_StartMysteryGiftParent
CommState_StartMysteryGiftChild
CommState_RequestShutdown
CommState_WasWifiDisconnected
CommState_GetWifiMatchStatus
CommState_EndPlazaMatch
CommState_IsWifiInLobby
TextFlags_BeginAutoScroll
TextFlags_EndAutoScroll
WifiConnection_StartVoiceChat
WifiConnection_StopVoiceChat
PlayFanfare
Comm_FindFriendSlot
NNS_G2dGetUnpackedPaletteData
StopSE
NNS_G3dGlbFlushP
NNS_G3dDraw
_f_lltof
NNS_G3dAnmObjDisableID
NNS_G3dGetResDictIdxByName
NNS_G3dAnmObjEnableID
NNS_G3dMdlSetMdlAmbAll
GF_SndHandleSetTempoRatio
WiFiPlazaNews_GetOpenTime
WiFiPlazaNews_GetSlotChange
WiFiPlazaNews_IsSlotOccupied
WiFiPlazaNews_GetSlotGender
WiFiPlazaNews_IsOwnSlot
WiFiPlazaNews_IsOldestSlot
WiFiPlazaNews_IsLastJoinedSlot
WiFiPlazaNews_GetEventSlot
WiFiPlazaNews_HasEvent
WiFiPlazaNews_GetEventType
WiFiPlazaNews_FormatEvent
WiFiPlazaNews_RemoveCurrentEvent
PokewalkerIr_Start
PokewalkerIr_SetIdentities
PokewalkerIr_SetCompressBuffer
PokewalkerApp_ShowMonPicture
IsFanfarePlayingOrWaiting
GF_SndStartFadeOutBGM
PokewalkerApp_StartSubBgScroll
PokewalkerIr_RequestEraseRecords
PokewalkerIr_Stop
PlayBGM
PokewalkerIr_IsConnected
GF_GetCurrentPlayingBGM
Comm_InitSyncedSave
Comm_SyncedSave
CommState_ResumeAlternateSearch
NNS_G3dInit
CommState_SetSpinTradeLinkMode
NNS_G3dRenderObjInit
NNS_G2dInitImagePaletteProxy
NNS_G2dInitImageProxy
NNS_G3dGeFlushBuffer
NNS_G2dSetupSoftwareSpriteCamera
PlayCryDelayed
FieldViewPhoto_GetAlbumScrollParam
FieldViewPhoto_SetPlayerInput
WifiClub_Init
WifiClub_Main
WifiClub_Exit
NNS_FndCreateExpHeapEx
NNS_FndDestroyExpHeap
NNS_FndAllocFromExpHeapEx
NNS_FndFreeToExpHeap
Comm_HasWifiProfile
CommState_InitWiiMessageWfcSession
CommState_FreeWiiMessageWfcSession
CommState_ShowRestartErrorAndHalt
Comm_GetOwnGsProfileId
CommState_FrameDummy
Save_FashionData_Get
Save_FashionData_GetFashionCase
FashionCase_CountAccessories
FashionCase_CountWallpapers
CommState_GetLinkMode
EasyChat_LaunchApp
NNS_G3dTex4x4GetRequiredSize
NNS_G3dBindMdlSet
NNS_G3dReleaseMdlSet
GF_SndGetFadeTimer
gChatStr_ciFreeCallbackData
gLobbyStr_ChatCallbacksC
gLobbyStr_DataNeNULL
gChatStr_DataTypeGe0
gChatStr_DataTypeLtCALLBACK_NUM
gChatStr_DataCallbackNeNULL
gChatStr_DataCallbackParamsNeNULL
gChatStr_DataID_Ge0
gChatStr_0_8
gChatStr_ciAddCallback_
gChatStr_ChatNeNULL
gChatStr_TypeGe0
gChatStr_TypeLtCALLBACK_NUM
gChatStr_ConnectionCallbackListNeNULL
gChatStr_CallbackNeNULL_2
gChatStr_CallbackParamsNeNULL
gChatStr_CallbackParamsSizeGt0
gChatStr_ID_Ge0
gChatStr_SrcParamsNumChannelsGe0
gChatStr_SrcParamsNumUsersNeNULL
gChatStr_SrcParamsNumUsersGe0
gChatStr_SrcParamsModesNeNULL
gChatStr_SrcParamsNumBansGe0
gChatStr_SrcParamsNumSuggestedNicksGe0
gChatStr_SrcParamsNumGe0
gChatStr_ciCallCallback
gChatStr_ciCallCallbacks
gChatStr_ciGetCallbackIndexByID
gChatStr_ciChannelEntering
gChatStr_ChatChannelC
gChatStr_ChatNeNULL_2
strnicmp
gChatStr_ciChannelLeft
gChatStr_ChannelNeNULL
gChatStr_Channel0_Ne0
gChatStr_StrlenChannelLtMAX_CHANNEL
gChatStr_ciChannelListUsersMap
gChatStr_ElemNeNULL
gChatStr_ClientDataNeNULL
gChatStr_UserNameNeNULL
gChatStr_UserName0_Ne0
gChatStr_StrlenUserNameLtMAX_NAME
gChatStr_DataNumUsersGe0
gChatStr_0_9
gChatStr_ciChannelListUsers
gChatStr_CallbackNeNULL_3
gChatStr_ciInChannel
gChatStr_ciJoinCallbackCalled
gChatStr_ciWasJoinCallbackCalled
gChatStr_ciGetChannelNumUsers
gChatStr_ciDestroyFilter
gChatStr_ChatHandlersC
gChatStr_FilterNeNULL
gChatStr_ciRemoveFilter
gChatStr_ChatNeNULL_3
gChatStr_ciFinishFilter
gChatStr_filter
gChatStr_FilterTypeGe0_AndFilterTypeLtNUM_TYPES
gChatStr_0_10
gChatStr_ciFilterTimedout
gChatStr_TimedOut
gChatStr_ciFilterThink
gChatStr_ciGetNextID
gChatStr_ciCheckFiltersForID
gChatStr_ID_Gt0
gChatStr_ciAddFilter
gChatStr_ciAddJOINFilter
gChatStr_PasswordNeNULL
gChatStr_StrlenPasswordLtMAX_PASSWORD
gChatStr_ciProcessServerMessage
gChatStr_ChatMainC
gChatStr_MessageNeNULL_2
ciServerMessageHandlers
ciNumServerMessageHandlers
gChatStr_ciHandleDisconnect
gChatStr_ChatNeNULL_4
gChatStr_ciThink
gChatStr_Disconnected
gChatStr_chatEnterChannelA
gChatStr_ChannelNeNULL_3
gChatStr_Channel0_Ne0_3
gChatStr_CallbacksNeNULL_2
gChatStr_Empty_21
gChatFmt_JOIN
gChatStr_chatLeaveChannelA
gChatFmt_PART
gChatStr_chatSetChannelLimitA
gChatStr_LimitGe0
gChatFmt_MODE_L
gChatFmt_MODE_L_2
gChatStr_chatGetChannelNumUsersA
gChatStr_ciEnumUsersCallback
gChatStr_NumUsersGe0
gChatStr_ParamNeNULL
gChatStr_DataCallbackNeNULL_3
gChatStr_chatEnumUsersA
gChatStr_CallbackNeNULL_4
gChatFmt_NAMES
gChatStr_ciBufferPreAppend
gChatStr_ChatSocketC
gChatStr_BufferNeNULL_3
gChatStr_BufferSizeGe0
gChatStr_BufferSizeBUFFER_INC_Eq0
gChatStr_BufferLengthGe0
gChatStr_BufferLengthLeBufferSize
gChatStr_LenGe0_2
gChatStr_LenLeSHRT_MAX
gChatStr_ciBufferClipFront
gChatStr_LenLeBufferLength
gChatStr_ciSocketThinkSend
gChatStr_SockNeNULL_2
gChatStr_SockConnectStateEqCiNotConnectedOrSock
gChatStr_SockInputQueueNeNULL
gChatStr_SockInputQueueSizeGe0
gChatStr_SockInputQueueSizeBUFFER_INC_Eq0
gChatStr_SockInputQueueLengthGe0
gChatStr_SockInputQueueLengthLeSockInputQueueSize
gChatStr_SockOutputQueueNeNULL
gChatStr_SockOutputQueueSizeGe0
gChatStr_SockOutputQueueSizeBUFFER_INC_Eq0
gChatStr_SockOutputQueueLengthGe0
gChatStr_SockOutputQueueLengthLeSockOutputQueueSize
gChatStr_SockConnectStateEqCiConnected
gChatStr_ciSocketThinkRecv
gChatStr_ciSocketThink
gChatStr_ciSocketSend
gChatStr_BufferNeNULL_2
gChatStr_ciSocketSendf
ciSendfBuffer
gChatStr_ciParseUser
gChatStr_0_12
gChatStr_ciParseParam
gChatStr_PTextNeNULL
gChatStr_MessageNeNULL_3
gChatStr_SpaceColon_2
gChatStr_Space_4
gChatStr_ciParseMessage
gChatStr_ciSocketRecv
ChooseStarter_LaunchApp
CommState_SetSelectedOrder
TrainerCard_LaunchApp
CommState_GetBeaconId
CommState_ShowErrorAndHalt
CommState_StartParentSession
CommState_StartChildSession
CommState_GetSelectedOrder
CommState_RestartScan
CommState_RecvJoinRequest
CommState_GetJoinMessageSize
CommState_RecvJoinReply
CommState_GetSyncPacketSize
CommState_RecvWifiDisconnect
WifiConnection_Shutdown
CommState_Main
CommState_CheckLinkError
WifiConnection_SendToAll
WifiConnection_SendToHost
WifiConnection_SendFromHost
WifiConnection_GetAid
CommState_HasWifiFailed
CommState_GetWifiError
Comm_FindFriendIndexOfPlayer
Battle_LaunchApp
CommState_StopForReset
CommState_IsLinkDown
IsCryFinished
PlayBGMDirect
PlaySE_SetPitch
FashionCase_CanGiveAccessory
FashionCase_HasWallpaper
FieldSystem_FollowMonShayminNeedsRevert
CommState_RefuseJoins
CommState_RefuseJoinsAndRestart
PlaySEWithFieldBank
GF_SndHandleMoveVolume
NNS_G3dMdlSetMdlFogEnableFlagAll
NNS_G3dResDefaultSetup
NNS_G3dMdlSetMdlAlphaAll
NNS_G3dMdlSetMdlDiffAll
NNS_G3dMdlSetMdlSpecAll
NNS_G3dMdlSetMdlEmiAll
NNS_G3dMdlSetMdlLightEnableFlagAll
NNS_G3dMdlSetMdlPolygonModeAll
NNS_G3dMdlSetMdlCullModeAll
NNS_G3dMdlSetMdlPolygonIDAll
NNS_G3dMdlSetMdlDepthTestCondAll
NNS_G3dMdlSetMdl1DotAll
NNS_G3dMdlSetMdlFarClipAll
NNS_G3dMdlSetMdlXLDepthUpdateAll
WeatherManager_SetWeather
WeatherManager_Delete
Sound_ResetFieldBGMStopOnce
WeatherManager_ChangeWeather
WeatherManager_New
PlayCryEx
FashionCase_GiveFashionItem
WeatherManager_GetWeather
CommState_SetError
NNS_GfdResetLnkTexVramState
NNS_GfdResetLnkPlttVramState
NNS_G3dMdlGetMdlFogEnableFlag
NNS_G3dDraw1Mat1Shp
NNS_FndAllocFromAllocator
Comm_RegisterLinkedFriends
Comm_StoreFriendFromPlayer
Comm_RecordPlayerLocations
ApricornBox_LaunchApp
ApricornBox_LaunchAppForJuice
PokeathlonRecords_Show
VoltorbFlip_LaunchApp
TextFlags_SetFastForwardTouchButtonHitbox
Field2DCommand_CheckStand
Field2DCommand_CheckTurn
Field2DCommand_CheckWalk
Field2DCommand_CheckRun
Field2DCommand_CheckPause
Field2DCommand_CheckBump
Field2DCommand_CheckWalkSlow
Field2DCommand_CheckWalkInPlace
Sound_ClearBGMPauseFlags
GF_SND_BGM_DisableCheck
GF_FadeStartMusicId
GF_NowStartMusicId
Sound_SetFieldBGM
Sound_SetFieldBGMStopOnce
Sound_PlayFieldBGM
BugContestSwapMon_LaunchApp
PokegearTownMap_LaunchApp
MapPreviewGraphic_GetIndex
MapPreviewGraphic_BeginShowImage
UseMail_CreateArgs
PalPad_LaunchApp
BerryPots_LaunchApp
UnownReport_LaunchApp
PartyMenu_LaunchApp_Gracidea
VsRecorder_LaunchApp
StopSEByHandle
RenderText
Comm_EnsureDwcUserData
gOverlayTemplate_Battle
Field2DObjectSprite_GetPaletteOffset
Field2DObjectSprite_IsVisible
Field2DObjectSpriteManager_LoadChar
Field2DCamera_CenterOnObject
Field2DObjectSpriteManager_Update
Field2DBgLayer_SetCamera
Field2DCamera_GetY
Field2DCamera_GetX
Field2DMoveQueue_Pop
Field2DCommandQueue_Pop
Field2DCamera_Init
Field2DObjectSpriteManager_NewWithShadow
Field2DBgLayer_New
Field2DCommandQueue_New
Field2DMoveQueue_New
Field2DObjectSpriteManager_Free
Field2DBgLayer_Free
Field2DCommandQueue_Free
Field2DMoveQueue_Free
Field2DObjectSpriteManager_UnloadChar
Field2DObjectSpriteManager_CreateSprite
Field2DObjectSprite_SetVisible
Field2DObjectSprite_Delete
Field2DMoveQueue_Push
WifiConnection_GetTargetFriendIndex
NintendoWifiConnection_LaunchApp
RegisterHallOfFame_LaunchApp
Sound_SetMasterVolume
StopAllSE
Credits_LaunchApp
NNS_GfdGetLnkTexVramManagerWorkSize
NNS_GfdGetLnkPlttVramManagerWorkSize
NNS_GfdInitLnkTexVramManager
NNS_GfdInitLnkPlttVramManager
NNS_GfdInitFrmTexVramManager
NNS_GfdInitFrmPlttVramManager
NNS_G2dInitRenderer
NNS_G2dInitRenderSurface
NNS_G2dAddRendererTargetSurface
NNS_G2dGetUnpackedPaletteCompressInfo
NNS_G2dLoadPaletteEx
NNS_G2dLoadPalette
NNS_G2dGetUnpackedCellBank
NNS_G2dGetUnpackedAnimBank
NNS_G2dLoadImage1DMapping
NNS_G2dLoadImage2DMapping
CommState_InitWfcSession
CommState_FreeWfcSession
GameStats_Update
GameStats_GetError
GameStats_Shutdown
GameStats_GetResponseLength
GameStats_Start
GameStats_PrepareRequest
Comm_MarkLocationSeen
NNS_FndGetTotalFreeSizeForExpHeap
NNS_FndInitAllocatorForExpHeap
NNS_FndGetSizeForMBlockExpHeap
NNS_FndResizeForMBlockExpHeap
LegendaryCinematic_LaunchApp
LobbyString_Compare
LobbyChannelMap_FindInsertPos
LobbyChannelMap_InsertNode
ov45_02255358
LobbyTreeIterator_Advance
LobbyTree_EraseRebalance
LobbyString_Destroy
LobbyCharVector_RecommendSize
LobbyCharVector_Reallocate
LobbyCharVector_Allocate
LobbyString_InitCopy
LobbyString_Assign
LobbyString_InitFromCStr
LobbyString_CompareCStr
gLobbyChatManager
gLobbyStr_normal
gLobbyStr_voice
gLobbyStr_ops
gLobbyStr_VoiceOps
gLobbyStr_invalid
gLobbyRequestVtable
LobbyU32Vector_Destroy
gLobbyChatNextRequestId
gLobbyEnterChannelRequestVtable
LobbyString_Equals
gLobbyStr_Empty_18
LobbyString_Concat
LobbyString_Substr
gLobbyStr___2
LobbyChat_SplitString
LobbyStringVector_Destroy
gLobbyMemOutOfMemoryHandler
gLobbyRequestMapInsertFlags
gLobbyStr_TreeInsertLengthError_3
LobbyTree_InsertRebalance
std::__vector_common<true>::throw_length_error()
LobbyUserIdSet_FindInsertPos
LobbyUserIdSet_CopyNodes
LobbyUserIdSet_Swap
LobbyUserIdSet_Destroy2
gLobbyStr_TreeInsertLengthError_4
LobbyUserIdSet_DestroyNodes
LobbyUserMap_FindInsertPos
LobbyString_ReplaceRange
gPPWLobbyWork
PPWLobby_MakeChannelName
PPWLobby_OnPlayerJoined
PPWLobby_OnPlayerLeft
PPWLobby_OnMessage
PPWLobby_OnChannelDataChanged
PPWLobby_OnTopicChanged
gPPWLobbyFootChannelMode
gPPWLobbyClockChannelMode
gPPWLobbyNewsChannelMode
PPWLobby_OnEnterSubChannel
PPWLobby_SetChannelData
Bytes::~Bytes()
MagnetTrainCutscene_LaunchApp
Comm_LoadAndInitDwc
CommState_GetErrorScreen
CommState_IsWifi
GF_MicPauseOnLidClose
GF_MicResumeOnLidOpen
NNS_G2dGetNewOamManagerInstance
NNS_G2dApplyAndResetOamManagerBuffer
NNS_G2dEntryOamManagerOamWithAffineIdx
NNS_G2dEntryOamManagerAffine
NNS_G2dLoadImageVramTransfer
CommState_SetEntry
SafariAreaCustomizer_LaunchApp
Bag_LaunchApp
NNS_SndPlayerSetPlayerVolume
NNS_SndPlayerSetPlayableSeqCount
NNS_SndPlayerSetAllocatableChannel
WifiSetupApp_Run
NNS_GfdDoVramTransfer
Sound_IsMicCounterFull
GF_MIC_StartAutoSampling
GF_MIC_StopAutoSampling
NNS_GfdSetFrmTexVramState
NNS_GfdFreeLnkTexVram
NNS_GfdSetFrmPlttVramState
NNS_GfdFreeLnkPlttVram
NNS_GfdGetFrmTexVramState
NNS_GfdGetFrmPlttVramState
Sound_ApplyFootstepVariation
GF_SndSetTrackPitchBySeqNo
NNS_G2dGetAnimCtrlCurrentElement
Sound_SetPlayerPause
_s32_div_f
GF_SndReadSeqVariable
PlaySEOnPlayer
SetPlayerPan
GF_SndCountPlayingSeqOnPlayer
GF_SndSetTrackPitchByPlayerNo
_fflt
NNS_G2dSetImageLocation
NNS_G2dSetImagePaletteLocation
PlaySE_SetVolume
PokeathlonCourse_LaunchApp
NNS_G2dDrawSpriteFast
FashionEntry_sizeof
Save_FashionData_GetEntry
Save_FashionData_InsertEntries
TextPrinter_SetDownArrowBaseTile
SafariDecoration_LaunchApp
Save_FashionData_sizeof
Save_FashionData_Init
CallTask_NamingScreen
BGM_SaveStateAndPlayNew
ScrCmd_ConditionalLinkSyncStart
ScrCmd_ConditionalLinkSyncWait
ScrCmd_ConditionalLinkSyncCreate
ScrCmd_ConditionalLinkSyncDestroy
ScrCmd_SetVarZero
ScrCmd_BeginTextAutoScroll
PartyMenu_LaunchApp_PartySelect
PartyMenu_LaunchApp_InGameTrade
TaskManager_LaunchPartyMenu_UnionRoomBattleSelect
PartyMenuArgs_GetSlot
MoveSelectionResult_GetMoveSlot
MoveSelectionResult_GetPartySlot
LearnForgetMove_LaunchApp
PokemonSummary_LaunchApp
Save_FashionData_IsEntrySaved
Save_FashionData_IsLargeEntrySaved
CallTask_DressUp
AccessoryPortrait_LaunchApp
FashionEntry_SetMailMessageWord
GeonetGlobe_LaunchApp
CallTask_BallCapsuleEditor
BattleRoomResults_LaunchApp
PCBox_LaunchApp
DrawingSession_LaunchApp
CallTask_WirelessTrade
RecordMixing_LaunchApp
HallOfFameShowcase_LaunchApp
Gts_LaunchApp
Bag_LaunchApp_WithPocket
BagView_SelectResult
CallTask_EasyChat
TrainerCardSignature_LaunchApp
CommState_SetUnionAppLinkMode
CommState_SetUnionRoomLinkMode
HatchEggInParty
Mailbox_LaunchApp
FashionCase_GetAccessoryCount
FashionCase_GiveContestBackground
Certificates_LaunchApp
FashionCase_TakeAccessory
PartyMenu_LaunchApp_DayCare
PartyMenuArgs_GetSelectedAction
FrontierRecords_LaunchApp
ScratchOffCards_LaunchApp
GlobalTerminal_LaunchApp
PokegearPhone_LaunchApp
FieldSystem_TakePhoto
FieldSystem_ViewSavedPhotos
PokeathlonMedals_LaunchApp
PokeathlonEventRecord_LaunchApp
PokeathlonTrophies_LaunchApp
PokeathlonCourseRecord_LaunchApp
AlphPuzzle_LaunchApp
Sound_LoadSceneSoundEffects
StartShayminRevertToLandForm
FieldSystem_BeginCelebiTimeTravelCutsceneTask
MoveRelearner_LaunchApp
GF_SndCountPlayingSeqBySeqNo
GF_SndStartFadeInBGM
PlayBGMOnBGMPlayerInField
GF_SND_BGM_DisableSet
GF_SetVolumeBySeqNo
FadeFunc_00
FadeFunc_01
FadeFunc_02
FadeFunc_03
FadeFunc_04
FadeFunc_05
FadeFunc_06
FadeFunc_07
FadeFunc_08
FadeFunc_09
FadeFunc_10
FadeFunc_11
FadeFunc_12
FadeFunc_13
FadeFunc_14
FadeFunc_15
FadeFunc_16
FadeFunc_17
FadeFunc_18
FadeFunc_19
FadeFunc_20
FadeFunc_21
FadeFunc_22
FadeFunc_23
FadeFunc_24
FadeFunc_25
FadeFunc_26
FadeFunc_27
FadeFunc_28
FadeFunc_29
FadeFunc_30
FadeFunc_31
FadeFunc_32
FadeFunc_33
FadeFunc_34
FadeFunc_35
FadeFunc_36
FadeFunc_37
FadeFunc_38
FadeFunc_39
FadeFunc_40
FadeFunc_41
FadeFunc_42
ShipDepartureCutscene_LaunchApp
NNS_SndInit
NNS_SndHeapCreate
NNS_SndArcInit
NNS_SndArcPlayerSetup
NNS_SndHeapGetSize
GF_SndSetMonoFlag
NNS_SndUpdateDriverInfo
NNS_SndPlayerReadDriverTrackInfo
NNS_SndReadDriverChannelInfo
NNS_SndMain
GF_SndGetAfterFadeDelayTimer
GF_SndPlayerCountPlayingSeqByPlayerNo
NNS_SndHeapSaveState
NNS_SndHeapGetFreeSize
NNS_SndArcLoadGroup
NNS_SndArcLoadBank
NNS_SndHandleInit
NNS_SndPlayerStopSeqByPlayerNo
NNS_SndHandleReleaseSeq
Sound_IsWaveOutPlaying
Sound_GetChatotBuffer
StopCry
Sound_AllocWaveOutChannel
Sound_GetWaveOutHandle
Sound_StartWaveOut
Sound_SetWaveOutVolume
Sound_StopWaveOutAndFreeBuffer
Sound_FreeWaveOutChannel
PlayCryDelayedEx
PartyMenu_LaunchApp_SpinTrade
PokemonSummary_CreateArgs
PokemonSummary_LearnForget_LaunchApp
NNS_G2dSetRndCoreAffineOverwriteMode
NNS_G2dSetRndCoreFlipMode
NNS_G2dFreeCellTransferStateHandle
NNS_G2dGetAnimSequenceByIdx
NNS_G2dSetCellAnimationSequence
NNS_G2dSetAnimSequenceToMCAnimation
NNS_G2dResetAnimCtrlState
NNS_G2dTickCellAnimation
NNS_G2dTickMCAnimation
NNS_G2dSetCellAnimationCurrentFrame
NNS_G2dSetMCAnimationCurrentFrame
NNS_G2dGetAnimCtrlCurrentFrame
NNS_G2dInitCellAnimation
NNS_G2dGetNewCellTransferStateHandle
NNS_G2dInitCellAnimationVramTransfered
NNS_G2dGetMCBankNumNodesRequired
NNS_G2dInitMCAnimation
NNS_G2dSetRendererImageProxy
NNS_G2dBeginRendering
NNS_G2dPushMtx
NNS_G2dTranslate
NNS_G2dScale
NNS_G2dRotZ
NNS_G2dDrawCellAnimation
NNS_G2dDrawMultiCellAnimation
NNS_G2dPopMtx
NNS_G2dEndRendering
NNS_G2dGetUnpackedMultiCellBank
NNS_G2dGetUnpackedMCAnimBank
Pokedex_LaunchApp
PartyMenu_LaunchApp_Field
UseMail_LaunchApp_Compose
UseMail_LaunchApp_ReadMonMail
Bag_LaunchApp_AllPockets
OptionsMenu_LaunchApp
NNS_G3dGetResDataByName
CommState_StartAlternateSearch
CommState_IsAlternateLinked
CommState_JoinRecordMixing
CommState_JoinSpinTrade
CommState_JoinParent
CommState_ReconnectToParent
CommState_IsAlternateSearchReady
CommState_ReportsErrors
CommState_StopAlternateSearch
CommState_GetJoinResult
CommState_SetLinkMode13RequestMode0
LaunchApp_ViewRankings
NNS_GfdInitVramTransferManager
NNS_GfdRegisterNewVramTransferTask
NNS_G2dInitCellTransferStateManager
NNS_G2dUpdateCellTransferStateManager
CommState_GetWifiClubData
Field2DObjectSpriteManager_New
WifiConnection_SetVoiceChatEnabled
WifiConnection_SetOwnStatusData
CommState_StartWifiMatch
CommState_SetWifiPeerLinkMode29
CommState_SetClubWifiLinkMode
Sound_ApplyVoiceChatVolumeForSeq
GF_SndHandleSetInitialVolume
WifiConnection_SetDisconnectCallback
Field2DObjectSpriteManager_IsCharLoaded
Field2DObjectSpriteManager_UnloadAllChars
Field2DObjectSprite_SetAutoUpdate
Field2DObjectSprite_StartLoopAnim
Field2DObjectSprite_UpdateAnim
Field2DObjectSprite_SyncAnim
WifiConnection_SetDisconnectWhenAlone
WifiConnection_ResetStopServerMatching
WifiConnection_StopServerMatching
CommState_LeaveWifiMatch
CommState_SetWifiClubLinkMode
WiFiPlazaHistory_Init
WiFiPlazaHistory_IsCorrupt
WiFiPlazaHistory_Add
WiFiPlazaNews_AddPlayer
WiFiPlaza_GetLocalTime
WiFiPlazaNews_SetOldestPlayer
WiFiPlazaHistory_ReplaceValue
WiFiPlazaNews_AddSpecialVisitorEvent
WiFiPlazaNews_RemovePlayer
WiFiPlazaNews_SetOpenTime
WiFiPlazaHistory_GetId
WiFiPlazaHistory_GetValue
WiFiPlazaNews_AddChatEvent
WiFiPlazaNews_AddTapToyTradeEvent
WiFiPlazaNews_AddMinigameEntryEvent
WiFiPlazaNews_AddFootprintStampEvent
WiFiPlazaNews_AddVisitorProfilesEvent
WiFiPlazaNews_AddPlazaNewsEvent
WiFiPlazaNews_AddMinigameResultEvent
WiFiPlazaNews_AddLobbyEventNotice
CommState_HasPlazaLobbyFailed
WiFiPlazaNewsSlots_Init
WiFiPlazaNewsEventList_Init
WiFiPlazaNewsEventList_Free
WiFiPlazaNewsEventList_Update
WiFiPlazaNewsSlots_SetChange
CommState_StartWifiPlaza
CommState_HasPlazaLobbyStarted
CommState_IsWifiJoiningLobby
CommState_LeaveWifiPlaza
CommState_GetPlazaMatchStatus
CommState_StartPlazaMatch
CommState_SetPlazaLinkMode
NNS_G3dReleaseMdlTex
NNS_G3dReleaseMdlPltt
_fadd
_ffix
NNS_G3dMdlGetMdlLightEnableFlag
Field2DCommandQueue_Push
PokeathlonBox_Init
PokeathlonBox_Main
PokeathlonBox_Exit
PokewalkerApp_Link_Enter
PokewalkerApp_Link_Update
PokewalkerApp_Link_Exit
PokewalkerApp_Link_Start
PokewalkerApp_Link_PrintPointAtCard
PokewalkerApp_Link_WaitPointAtCard
PokewalkerApp_Link_FadeOutToSelectMon
PokewalkerApp_Link_WaitFadeOutToSelectMon
PokewalkerApp_Link_FadeIn
PokewalkerApp_Link_WaitFadeIn
PokewalkerApp_Link_PrintSelectCourse
PokewalkerApp_Link_WaitSelectCourse
PokewalkerApp_Link_ShowCourseMenu
PokewalkerApp_Link_HandleCourseMenu
PokewalkerApp_Link_EndCourseMenu
PokewalkerApp_Link_PrintConfirmCourse
PokewalkerApp_Link_WaitConfirmCourse
PokewalkerApp_Link_ShowConfirmCourseYesNo
PokewalkerApp_Link_HandleConfirmCourseYesNo
PokewalkerApp_Link_PrintInstructions
PokewalkerApp_Link_WaitInstructions
PokewalkerApp_Link_PrintWillConnectWithMon
PokewalkerApp_Link_PrintWillConnect
PokewalkerApp_Link_PrintPointAtCardForStroll
PokewalkerApp_Link_WaitPointAtCardForStroll
PokewalkerApp_Link_PrintPointAtCardForGifts
PokewalkerApp_Link_WaitPointAtCardForGifts
PokewalkerApp_Link_StartUpload
PokewalkerApp_Link_UpdateUpload
PokewalkerApp_Link_StartReceive
PokewalkerApp_Link_UpdateReceive
PokewalkerApp_Link_StartReceiveGifts
PokewalkerApp_Link_UpdateReceiveGifts
PokewalkerApp_Link_StartSendStroll
PokewalkerApp_Link_UpdateSendStroll
PokewalkerApp_Link_PrintError
PokewalkerApp_Link_PrintCanceled
PokewalkerApp_Link_WaitError
PokewalkerApp_Link_PrintWentToWalker
PokewalkerApp_Link_WaitWentToWalker
PokewalkerApp_Link_PrintReturned
PokewalkerApp_Link_WaitReturned
PokewalkerApp_Link_PrintReleased
PokewalkerApp_Link_WaitReleased
PokewalkerApp_Link_PrintReceivedGifts
PokewalkerApp_Link_WaitReceivedGifts
PokewalkerApp_Link_PrintSendBackCaution
PokewalkerApp_Link_WaitSendBackCaution
PokewalkerApp_Link_ShowSendBackYesNo
PokewalkerApp_Link_HandleSendBackYesNo
PokewalkerApp_Link_PrintWillSendBack
PokewalkerApp_Link_WaitWillSendBack
PokewalkerApp_Link_ShowSendBackMenu
PokewalkerApp_Link_HandleSendBackMenu
PokewalkerApp_Link_EndSendBackMenu
PokewalkerApp_Link_PrintComeBack
PokewalkerApp_Link_StartReturnAnimation
PokewalkerApp_Link_AnimateReturn
PokewalkerApp_Link_AfterReturnAnimation
PokewalkerApp_Link_PrintArrived
PokewalkerApp_Link_EndSendBack
PokewalkerApp_Link_RetryInstructions
PokewalkerApp_Link_RetryPointAtCardForStroll
PokewalkerApp_Link_RetryPointAtCard
PokewalkerApp_Link_RetryPointAtCardForGifts
PokewalkerApp_Link_FadeOut
PokewalkerApp_Link_WaitFadeOut
PokewalkerApp_Link_WaitMessage
PokewalkerApp_Link_WaitCursor
PokewalkerApp_Link_End
NintendoWfcUnused_SessionInit
NintendoWfcUnused_SessionMain
NintendoWfcUnused_SessionExit
AllocTexVram_
FreeTexVram_
AllocPlttVram_
FreePlttVram_
CpuLoadOAMMain_
CpuLoadOAMSub_
NNSi_G3dAnmCalcNsBva
NNSi_G3dAnmCalcNsBca
NNSi_G3dAnmCalcNsBta
NNSi_G3dAnmCalcNsBtp
NNSi_G3dAnmCalcNsBma
NNSi_G3dAnmBlendVis
NNSi_G3dAnmBlendJnt
NNSi_G3dAnmBlendMat
NNSi_G3dAnmObjInitNsBma
NNSi_G3dAnmObjInitNsBtp
NNSi_G3dAnmObjInitNsBta
NNSi_G3dAnmObjInitNsBva
NNSi_G3dAnmObjInitNsBca
NNSi_G3dSendJointSRTBasic
NNSi_G3dSendJointSRTMaya
NNSi_G3dSendJointSRTSi3d
NNSi_G3dGetJointScaleBasic
NNSi_G3dGetJointScaleMaya
NNSi_G3dGetJointScaleSi3d
NNSi_G3dSendTexSRTMaya
NNSi_G3dSendTexSRTSi3d
NNSi_G3dSendTexSRT3dsMax
NNSi_G3dSendTexSRTXsi
NNSi_G3dFuncSbc_MAT_InternalDefault
NNSi_G3dFuncSbc_SHP_InternalDefault
NNSi_G3dFuncSbc_NOP
NNSi_G3dFuncSbc_RET
NNSi_G3dFuncSbc_NODE
NNSi_G3dFuncSbc_MTX
NNSi_G3dFuncSbc_MAT
NNSi_G3dFuncSbc_SHP
NNSi_G3dFuncSbc_NODEDESC
NNSi_G3dFuncSbc_BB
NNSi_G3dFuncSbc_BBY
NNSi_G3dFuncSbc_NODEMIX
NNSi_G3dFuncSbc_CALLDL
NNSi_G3dFuncSbc_POSSCALE
NNSi_G3dFuncSbc_ENVMAP
NNSi_G3dFuncSbc_PRJMAP
texmtxCalc_flag_
texmtxCalc_flagS_
texmtxCalc_flagR_
texmtxCalc_flagRS_
texmtxCalc_flagT_
texmtxCalc_flagTS_
texmtxCalc_flagTR_
texmtxCalc_flagTRS_
texmtxCalc_flag___3dsmax
texmtxCalc_flagS___3dsmax
texmtxCalc_flagR___3dsmax
texmtxCalc_flagRS___3dsmax
texmtxCalc_flagT___3dsmax
texmtxCalc_flagTS___3dsmax
texmtxCalc_flagTR___3dsmax
texmtxCalc_flagTRS___3dsmax
texmtxCalc_flag___xsi
texmtxCalc_flagS___xsi
texmtxCalc_flagR___xsi
texmtxCalc_flagRS___xsi
texmtxCalc_flagT___xsi
texmtxCalc_flagTS___xsi
texmtxCalc_flagTR___xsi
texmtxCalc_flagTRS___xsi
```

## Work queue for the next slice

1. **`libnitrosystem.a` (host32 and 3ds).** The generated `nns_all_headers.c`
   includes the NNS SDK's `snd/main.h` and `snd_internal.h` together with
   the port's `nitro/snd` compat headers; their `SND_*` declarations
   disagree. This is a port-layer/`hgss_nitro.py` fix (exclude the internal
   sound headers from the unity file or give the compat header matching
   prototypes), not game code.
2. **3DS ARM7 sound assembly.** `ds_sound_tables.c` reaches the assembler
   with MWCC-style `.type` text left by the N7_ rewriting step.
3. **The 50 host32 compile failures.** They are the Phase 2 GCC-fix patch
   series: declare implicit functions, fix `MATHRandContext32` duplicates,
   label/declaration order, anonymous structs, and the RC4/CRC assembly
   bodies. Each turns into one guarded patch in `patches/pokeheartgold/`.
4. **The unresolved list.** Most entries are objects that failed to
   compile (`CommState_*`, `PokewalkerApp_*`, `FadeFunc_*`, `Field*`,
   `WiFiPlazaNews_*`, the `gChatStr`/`gLobbyStr` C++ globals) plus the
   missing NitroSystem archive. Fixing 1 and 3 removes most in one pass.
5. **`lib/MSL_C/asm/*.s`.** They still assemble (150/150), but are not
   linked: Metrowerks' errno/ctype/printf data clashes with glibc (errno
   is TLS). Replace them with newlib or small C, per the Phase 2 plan.
6. **DWC/WiFi/CPS/VCT/dsprot asm data** is built but kept out of the link
   (the stubbed C sources replace those libraries). Their symbols are in
   the unresolved list; decide per symbol between a stub and real data.
7. **Weak duplicate pass.** Two cross-overlay/global names are now weak
   (`FrontierScript_ReadByte`, `sListMenuTemplate`). They need the same
   treatment Platinum gives header inlines, or one of the two copies must
   become local. Keep the census output in the build log visible.

## Notes and limitations

- `main_pc.c` is a link target, not a runner: it logs, counts frames and
  exits. `Heartgold_Run` sets a HeartGold cartridge header (IPGE) and the
  save/data-pack paths but does not boot the game.
- The 3DS target has no shell and produces no `.3dsx`; it writes
  `build/game-3ds/pokeheartgold-3ds.elf` if a future link ever succeeds.
- `build/upstream` was being re-bootstrapped by the lead during this
  slice. One intermediate host run compiled 571/1094 because the tree was
  half-written; that run is discarded. The numbers above are from the
  stable rerun. Re-measure after the patch stack settles.
- No game data, ROM, save or binary is written into the repo. Outputs are
  under `build/game-{pc,3ds}` (or the `--out` directory) and `/work/out`.
