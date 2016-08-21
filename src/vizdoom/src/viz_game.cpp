/*
 Copyright (C) 2016 by Wojciech Jaśkowski, Michał Kempka, Grzegorz Runc, Jakub Toczek, Marek Wydmuch

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
*/

#include "viz_game.h"
#include "viz_defines.h"
#include "viz_depth.h"
#include "viz_labels.h"
#include "viz_message_queue.h"
#include "viz_screen.h"
#include "viz_shared_memory.h"

#include "d_netinf.h"
#include "d_event.h"
#include "g_game.h"
#include "c_dispatch.h"
#include "p_acs.h"

EXTERN_CVAR (Bool, viz_debug)
EXTERN_CVAR (Int, viz_screen_format)
EXTERN_CVAR (Bool, viz_depth)
EXTERN_CVAR (Bool, viz_labels)
EXTERN_CVAR (Bool, viz_automap)
EXTERN_CVAR (Bool, viz_loop_map)
EXTERN_CVAR (Bool, viz_override_player)

VIZGameState *vizGameStateSM = NULL;

int VIZ_CheckItem(FName name) {
    if(VIZ_PLAYER.mo != NULL) {
        AInventory *item = VIZ_PLAYER.mo->FindInventory(name);
        if(item != NULL) return item->Amount;
    }
    return 0;
}

int VIZ_CheckItem(const PClass *type) {
    if(VIZ_PLAYER.mo != NULL) {
        AInventory *item = VIZ_PLAYER.mo->FindInventory(type);
        if (item != NULL) return item->Amount;
    }
    return 0;
}

int VIZ_CheckWeaponAmmo(AWeapon* weapon){
    if(weapon != NULL) return VIZ_CheckItem(weapon->AmmoType1);
    return -1;
}

int VIZ_CheckSelectedWeapon(){

    if(VIZ_PLAYER.ReadyWeapon == NULL) return -1;

    const PClass *type1 = VIZ_PLAYER.ReadyWeapon->GetClass();
    if(type1 == NULL) return -1;

    for(int i=0; i< VIZ_GV_SLOTS_SIZE; ++i){
        for(int j = 0; j < VIZ_PLAYER.weapons.Slots[i].Size(); ++j){
            const PClass *type2 = VIZ_PLAYER.weapons.Slots[i].GetWeapon(j);
            //if(strcmp(type1->TypeName.GetChars(), type2->TypeName.GetChars()) == 0) return i;
            if(type1 == type2) return i;
        }
    }

    return -1;
}

int VIZ_CheckSelectedWeaponAmmo(){
    return VIZ_CheckWeaponAmmo(VIZ_PLAYER.ReadyWeapon);
}

int VIZ_CheckSlotAmmo(unsigned int slot){
    if(VIZ_PLAYER.weapons.Slots[slot].Size() <= 0) return 0;

    const PClass *typeWeapon = VIZ_PLAYER.weapons.Slots[slot].GetWeapon(0);
    AWeapon *weapon = (AWeapon*) typeWeapon->CreateNew();
    //AWeapon *weapon = (AWeapon*)VIZ_PLAYER.mo->FindInventory(type);
    if (weapon != NULL){
        const PClass *typeAmmo = weapon->AmmoType1;
        weapon->Destroy();
        return VIZ_CheckItem(typeAmmo);
    }
    else return 0;
}

int VIZ_CheckSlotWeapons(unsigned int slot){
    int inSlot = 0;
    for(int i = 0; i < VIZ_PLAYER.weapons.Slots[slot].Size(); ++i){
        const PClass *type = VIZ_PLAYER.weapons.Slots[slot].GetWeapon(i);
        inSlot += VIZ_CheckItem(type);
    }
    return inSlot;
}

void VIZ_GameStateInit(){

    try {
        VIZSMRegion* gameStateRegion = &vizSMRegion[0];
        VIZ_SMCreateRegion(gameStateRegion, false, 0, sizeof(VIZGameState));
        vizGameStateSM = static_cast<VIZGameState *>(gameStateRegion->address);

        VIZ_DEBUG_PRINT("VIZ_GameStateInit: gameStateOffset: %zu, gameStateSize: %zu\n",
                        gameStateRegion->offset, sizeof(VIZGameState));
    }
    catch(bip::interprocess_exception &ex){
        VIZ_ReportError("VIZ_GameStateInit", "Failed to create game state.");
    }

    vizGameStateSM->VERSION = VIZ_VERSION;
    strncpy(vizGameStateSM->VERSION_STR, VIZ_VERSION_STR, 8);
    vizGameStateSM->SM_SIZE = vizSMSize;

    VIZ_DEBUG_PRINT("VIZ_GameStateInit: VERSION %d, VERSION_STR: %s, \n", vizGameStateSM->VERSION, vizGameStateSM->VERSION_STR);
}

void VIZ_GameStateUpdate(){
    vizGameStateSM->SM_SIZE = vizSMSize;

    vizGameStateSM->SCREEN_WIDTH = vizScreenWidth;
    vizGameStateSM->SCREEN_HEIGHT = vizScreenHeight;
    vizGameStateSM->SCREEN_PITCH = vizScreenPitch;
    vizGameStateSM->SCREEN_SIZE = vizScreenSize;
    vizGameStateSM->SCREEN_FORMAT = *viz_screen_format;

    vizGameStateSM->DEPTH_BUFFER = *viz_depth && vizDepthMap;
    vizGameStateSM->LABELS = *viz_labels && vizLabels;
    vizGameStateSM->AUTOMAP = *viz_automap;

    for(int i = 0; i < VIZ_SM_REGION_COUNT; ++i){
        vizGameStateSM->SM_REGION_OFFSET[i] = vizSMRegion[i].offset;
        vizGameStateSM->SM_REGION_SIZE[i] = vizSMRegion[i].size;
        vizGameStateSM->SM_REGION_WRITEABLE[i] = vizSMRegion[i].writeable;
    }
}

void VIZ_GameStateTic(){

    VIZ_DEBUG_PRINT("VIZ_GameStateTic: tic %d, netgame: %d, multiplayer: %d, recording: %d, playback: %d, in_level: %d, map_tic: %d\n",
                    gametic, netgame, multiplayer, demorecording, demoplayback, gamestate != GS_LEVEL, level.maptime);

    vizGameStateSM->GAME_TIC = (unsigned int)gametic;
    vizGameStateSM->GAME_STATE = gamestate;
    vizGameStateSM->GAME_ACTION = gameaction;
    vizGameStateSM->GAME_STATIC_SEED = staticrngseed;
    vizGameStateSM->GAME_SETTINGS_CONTROLLER = VIZ_PLAYER.settings_controller;
    vizGameStateSM->GAME_NETGAME = netgame;
    vizGameStateSM->GAME_MULTIPLAYER = multiplayer;
    vizGameStateSM->DEMO_RECORDING = demorecording;
    vizGameStateSM->DEMO_PLAYBACK = demoplayback;

    vizGameStateSM->MAP_START_TIC = (unsigned int)level.starttime;
    vizGameStateSM->MAP_TIC = (unsigned int)level.maptime;

    for(int i = 0; i < VIZ_GV_USER_COUNT; ++i){
        vizGameStateSM->MAP_USER_VARS[i] = ACS_GlobalVars[i+1];
    }

    vizGameStateSM->MAP_END = gamestate != GS_LEVEL;
    if(vizGameStateSM->MAP_END) vizGameStateSM->PLAYER_DEATHCOUNT = 0;
//    if(*viz_loop_map && !level.MapName.Compare(level.NextMap)){
//        level.NextMap = level.MapName;
//        level.NextSecretMap = level.MapName;
//    }

    vizGameStateSM->MAP_REWARD = ACS_GlobalVars[0];

    bool prevDead = vizGameStateSM->PLAYER_DEAD;
    vizGameStateSM->PLAYER_READY_TO_RESPAWN = VIZ_PLAYER.playerstate == PST_REBORN;

    if(VIZ_PLAYER.mo != NULL) {
        vizGameStateSM->PLAYER_HAS_ACTOR = true;
        vizGameStateSM->PLAYER_DEAD = VIZ_PLAYER.playerstate == PST_DEAD || VIZ_PLAYER.mo->health <= 0;
    }
    else {
        vizGameStateSM->PLAYER_HAS_ACTOR = false;
        vizGameStateSM->PLAYER_DEAD = true;
    }

    if(vizGameStateSM->PLAYER_DEAD && !prevDead) ++vizGameStateSM->PLAYER_DEATHCOUNT;

    strncpy(vizGameStateSM->PLAYER_NAME, VIZ_PLAYER.userinfo.GetName(), VIZ_MAX_PLAYER_NAME_LEN);

    vizGameStateSM->MAP_KILLCOUNT = level.killed_monsters;
    vizGameStateSM->MAP_ITEMCOUNT = level.found_items;
    vizGameStateSM->MAP_SECRETCOUNT = level.found_secrets;

    vizGameStateSM->PLAYER_KILLCOUNT = VIZ_PLAYER.killcount;
    vizGameStateSM->PLAYER_ITEMCOUNT = VIZ_PLAYER.itemcount;
    vizGameStateSM->PLAYER_SECRETCOUNT = VIZ_PLAYER.secretcount;
    vizGameStateSM->PLAYER_FRAGCOUNT = VIZ_PLAYER.fragcount;

    vizGameStateSM->PLAYER_ATTACK_READY = (VIZ_PLAYER.WeaponState & WF_WEAPONREADY) != 0;
    vizGameStateSM->PLAYER_ALTATTACK_READY = (VIZ_PLAYER.WeaponState & WF_WEAPONREADYALT) != 0;
    vizGameStateSM->PLAYER_ON_GROUND = VIZ_PLAYER.onground;

    if (vizGameStateSM->PLAYER_HAS_ACTOR) vizGameStateSM->PLAYER_HEALTH = VIZ_PLAYER.mo->health;
    else vizGameStateSM->PLAYER_HEALTH = VIZ_PLAYER.health;

    vizGameStateSM->PLAYER_ARMOR = VIZ_CheckItem(NAME_BasicArmor);

    vizGameStateSM->PLAYER_SELECTED_WEAPON_AMMO = VIZ_CheckSelectedWeaponAmmo();
    vizGameStateSM->PLAYER_SELECTED_WEAPON = VIZ_CheckSelectedWeapon();

    for (unsigned int i = 0; i < VIZ_GV_SLOTS_SIZE; ++i) {
        vizGameStateSM->PLAYER_AMMO[i] = VIZ_CheckSlotAmmo(i);
        vizGameStateSM->PLAYER_WEAPON[i] = VIZ_CheckSlotWeapons(i);
    }

    vizGameStateSM->PLAYER_NUMBER = (unsigned int)consoleplayer;
    vizGameStateSM->PLAYER_COUNT = 1;
    if(netgame || multiplayer) {

        //VIZ_DEBUG_PRINT("VIZ_GameStateTic: tic %d, players: \n", gametic);

        vizGameStateSM->PLAYER_COUNT = 0;
        for (unsigned int i = 0; i < VIZ_MAX_PLAYERS; ++i) {
            if(playeringame[i]){
                ++vizGameStateSM->PLAYER_COUNT;
                vizGameStateSM->PLAYER_N_IN_GAME[i] = true;
                strncpy(vizGameStateSM->PLAYER_N_NAME[i], players[i].userinfo.GetName(), VIZ_MAX_PLAYER_NAME_LEN);
                vizGameStateSM->PLAYER_N_FRAGCOUNT[i] = players[i].fragcount;

                //VIZ_DEBUG_PRINT("playernum: %d, name: %s\n", i, vizGameStateSM->PLAYERS_NAME[i]);
            }
            else{
                strncpy(vizGameStateSM->PLAYER_N_NAME[i], players[i].userinfo.GetName(), VIZ_MAX_PLAYER_NAME_LEN);
                vizGameStateSM->PLAYER_N_FRAGCOUNT[i] = 0;
            }
        }
    }
}

void VIZ_GameStateUpdateLabels(){

    unsigned int labelCount = 0;
    if(vizLabels!=NULL){

        VIZ_DEBUG_PRINT("VIZ_GameStateUpdateLabels: tic: %d, number of sprites: %d\n",
                        gametic, vizLabels->getSprites().size());

        //TODO sort vizLabels->sprites

        for(auto i = vizLabels->sprites.begin(); i != vizLabels->sprites.end(); ++i){
            if(i->labeled){
                vizGameStateSM->LABEL[labelCount].objectId = i->actorId;
                strncpy(vizGameStateSM->LABEL[labelCount].objectName, i->actor->GetClass()->TypeName.GetChars(), VIZ_MAX_LABEL_NAME_LEN);
                vizGameStateSM->LABEL[labelCount].value = i->label;

                VIZ_DEBUG_PRINT("VIZ_GameStateUpdateLabels: labelCount: %d, objectId: %d, objectName: %s, value %d\n",
                                labelCount+1, vizGameStateSM->LABEL[labelCount].objectId,
                                vizGameStateSM->LABEL[labelCount].objectName, vizGameStateSM->LABEL[labelCount].value);

                ++labelCount;
            }
            if(i->label == VIZ_MAX_LABELS - 1 || labelCount >= VIZ_MAX_LABELS) break;
        }
    }

    vizGameStateSM->LABEL_COUNT = labelCount;
}

void VIZ_GameStateClose(){
    if(vizSMRegion[0].region) delete vizSMRegion[0].region;
}


