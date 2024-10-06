/*
===========================================================================
Copyright (C) 2023 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// g_bot.cpp

#include "g_local.h"
#include "entity.h"
#include "playerbot.h"
#include "g_bot.h"

static gentity_t   *firstBot          = NULL;
static saved_bot_t *saved_bots        = NULL;
static unsigned int num_saved_bots    = 0;
static unsigned int current_bot_count = 0;
static char       **modelList         = NULL;

Container<str> alliedModelList;
Container<str> germanModelList;

bool IsAlliedPlayerModel(const char *filename)
{
    return !Q_stricmpn(filename, "/allied_", 8) || !Q_stricmpn(filename, "/american_", 10);
}

bool IsGermanPlayerModel(const char *filename)
{
    return !Q_stricmpn(filename, "/german_", 8) || !Q_stricmpn(filename, "/IT_", 4) || !Q_stricmpn(filename, "/SC_", 4);
}

bool IsPlayerModel(const char *filename)
{
    size_t len = strlen(filename);

    if (len >= 8 && !Q_stricmp(&filename[len - 8], "_fps.tik")) {
        return false;
    }

    if (!IsAlliedPlayerModel(filename) && !IsGermanPlayerModel(filename)) {
        return false;
    }

    return true;
}

void ClearModelList()
{
    alliedModelList.FreeObjectList();
    germanModelList.FreeObjectList();
}

void InitModelList()
{
    char **fileList;
    int    numFiles;
    int    i;
    size_t numAlliedModels = 0, numGermanModels = 0;
    byte  *p;

    ClearModelList();

    fileList = gi.FS_ListFiles("models/player", ".tik", qfalse, &numFiles);

    for (i = 0; i < numFiles; i++) {
        const char *filename = fileList[i];

        if (!IsPlayerModel(filename)) {
            continue;
        }

        if (IsAlliedPlayerModel(filename)) {
            numAlliedModels++;
        } else {
            numGermanModels++;
        }
    }

    alliedModelList.Resize(numAlliedModels);
    germanModelList.Resize(numGermanModels);

    for (i = 0; i < numFiles; i++) {
        const char *filename = fileList[i];
        size_t      len      = strlen(filename);

        if (!IsPlayerModel(filename)) {
            continue;
        }

        if (IsAlliedPlayerModel(filename)) {
            alliedModelList.AddObject(str(filename + 1, 0, len - 5));
        } else {
            germanModelList.AddObject(str(filename + 1, 0, len - 5));
        }
    }

    gi.FS_FreeFileList(fileList);
}

void G_BotBegin(gentity_t *ent)
{
    level.m_bSpawnBot = true;
    G_ClientBegin(ent, NULL);
}

void G_BotThink(gentity_t *ent, int msec)
{
    usercmd_t  ucmd;
    usereyes_t eyeinfo;
    PlayerBot *bot;

    assert(ent);
    assert(ent->entity);
    assert(ent->entity->IsSubclassOfBot());

    bot = (PlayerBot *)ent->entity;

    bot->UpdateBotStates();
    bot->GetUsercmd(&ucmd);
    bot->GetEyeInfo(&eyeinfo);

    G_ClientThink(ent, &ucmd, &eyeinfo);
}

gentity_t *G_GetFirstBot()
{
    return firstBot;
}

void G_AddBot(unsigned int num, saved_bot_t *saved)
{
    int        n;
    int        i;
    int        clientNum = -1;
    gentity_t *e;
    char       botName[MAX_NETNAME];
    char       challenge[MAX_STRING_TOKENS];
    Event     *teamEv;

    num = Q_min(num, sv_maxbots->integer);
    for (n = 0; n < num; n++) {
        char userinfo[MAX_INFO_STRING] {0};

        for (i = maxclients->integer; i < game.maxclients; i++) {
            e = &g_entities[i];

            if (!e->inuse && e->client) {
                clientNum = i;
                break;
            }
        }

        if (clientNum == -1) {
            gi.Printf("No free slot for a bot\n");
            return;
        }

        if (gi.Argc() > 2) {
            Q_strncpyz(botName, gi.Argv(2), sizeof(botName));
        } else {
            Com_sprintf(botName, sizeof(botName), "bot%d", clientNum - maxclients->integer + 1);
        }

        Com_sprintf(challenge, sizeof(challenge), "%d", clientNum - maxclients->integer + 1);

        e->s.clientNum = clientNum;
        e->s.number    = clientNum;

        if (saved) {
            strncpy(userinfo, saved->pers.userinfo, ARRAY_LEN(userinfo));
        } else {
            Info_SetValueForKey(userinfo, "name", botName);

            //
            // Choose a random model
            //
            if (alliedModelList.NumObjects()) {
                Info_SetValueForKey(userinfo, "dm_playermodel", alliedModelList[rand() % alliedModelList.NumObjects()]);
            }
            if (germanModelList.NumObjects()) {
                Info_SetValueForKey(
                    userinfo, "dm_playergermanmodel", germanModelList[rand() % germanModelList.NumObjects()]
                );
            }

            Info_SetValueForKey(userinfo, "fov", "80");
            Info_SetValueForKey(userinfo, "protocol", "8");
            Info_SetValueForKey(userinfo, "ip", "0.0.0.0");
            Info_SetValueForKey(userinfo, "qport", "0");
            Info_SetValueForKey(userinfo, "challenge", challenge);
            Info_SetValueForKey(userinfo, "snaps", "1");
            Info_SetValueForKey(userinfo, "rate", "1");
            Info_SetValueForKey(userinfo, "dmprimary", "smg");
        }

        current_bot_count++;

        G_BotConnect(clientNum, userinfo);

        if (saved) {
            e->client->pers = saved->pers;
        }

        if (!firstBot) {
            firstBot = e;
        }

        G_BotBegin(e);

        if (saved) {
            /*
			switch (saved->team)
			{
            case TEAM_ALLIES:
                teamEv = new Event(EV_Player_JoinDMTeam);
				teamEv->AddString("allies");
				break;
            case TEAM_AXIS:
                teamEv = new Event(EV_Player_JoinDMTeam);
                teamEv->AddString("axis");
				break;
            default:
				teamEv = new Event(EV_Player_AutoJoinDMTeam);
				break;
			}
			*/
        } else {
            teamEv = new Event(EV_Player_AutoJoinDMTeam);
            e->entity->PostEvent(teamEv, level.frametime);

            Event *ev = new Event(EV_Player_PrimaryDMWeapon);
            ev->AddString("auto");

            e->entity->PostEvent(ev, level.frametime);
        }
    }
}

void G_RemoveBot(unsigned int num)
{
    unsigned int removed = 0;
    unsigned int n;
    unsigned int teamCount[2]{ 0 };
    bool bNoMoreToRemove = false;

    num = Q_min(num, sv_maxbots->integer);

    teamCount[0] = dmManager.GetTeamAllies()->m_players.NumObjects();
    teamCount[1] = dmManager.GetTeamAxis()->m_players.NumObjects();

    while (!bNoMoreToRemove) {
        bNoMoreToRemove = true;

        for (n = 0; n < game.maxclients && removed < num; n++) {
            gentity_t *e = &g_entities[game.maxclients - sv_maxbots->integer + n];
            if (e->inuse && e->client) {
                Player* player = static_cast<Player*>(e->entity);
                if (player->GetTeam() == TEAM_ALLIES || player->GetTeam() == TEAM_AXIS) {
                    unsigned int teamIndex = (player->GetTeam() - TEAM_ALLIES);
                    if (teamCount[teamIndex] < teamCount[1 - teamIndex]) {
                        // Skip bots in the lowest team
                        continue;
                    }

                    teamCount[teamIndex]--;
                    bNoMoreToRemove = false;
                }

                G_ClientDisconnect(e);
                current_bot_count--;
                removed++;
            }
        }
    }

    //
    // Remove all bots regardless
    //
    for (n = 0; n < game.maxclients && removed < num; n++) {
        gentity_t *e = &g_entities[game.maxclients - sv_maxbots->integer + n];
        if (e->inuse && e->client) {
            G_ClientDisconnect(e);
            current_bot_count--;
            removed++;
        }
    }
}

void G_SaveBots()
{
    unsigned int n;

    if (saved_bots) {
        delete[] saved_bots;
        saved_bots = NULL;
    }

    if (!current_bot_count) {
        return;
    }

    saved_bots = new saved_bot_t[current_bot_count];
    num_saved_bots = 0;
    for (n = 0; n < game.maxclients; n++) {
        gentity_t   *e     = &g_entities[game.maxclients - sv_maxbots->integer + n];

        if (e->inuse && e->client) {
            Player *player = static_cast<Player *>(e->entity);
            saved_bot_t& saved = saved_bots[num_saved_bots++];

            saved.bValid = true;
            //saved.team = player->GetTeam();
            saved.pers = player->client->pers;
        }
    }
}

void G_RestoreBots()
{
    unsigned int n;

    if (!saved_bots) {
        return;
    }

    for (n = 0; n < num_saved_bots; n++) {
        saved_bot_t& saved = saved_bots[n];

        G_AddBot(1, &saved);
    }

    delete[] saved_bots;
    saved_bots = NULL;
}

int G_CountClients()
{
    gentity_t   *other;
    unsigned int n;
    unsigned int count = 0;

    for (n = 0; n < maxclients->integer; n++) {
        other = &g_entities[n];
        if (other->inuse && other->client) {
            Player *p = static_cast<Player *>(other->entity);
            if (p->GetTeam() == teamtype_t::TEAM_NONE || p->GetTeam() == teamtype_t::TEAM_SPECTATOR) {
                // ignore spectators
                continue;
            }

            count++;
        }
    }

    return count;
}

void G_ResetBots()
{
    G_SaveBots();

    current_bot_count = 0;
}

void G_SpawnBots()
{
    unsigned int numClients;
    unsigned int numBotsToSpawn;

    InitModelList();

    if (saved_bots) {
        G_RestoreBots();
    }

    //
    // Check the minimum bot count
    //
    numClients = G_CountClients();
    if (numClients < sv_minPlayers->integer) {
        numBotsToSpawn = sv_minPlayers->integer - numClients + sv_numbots->integer;
    } else {
        numBotsToSpawn = sv_numbots->integer;
    }

    //
    // Spawn bots
    //
    if (numBotsToSpawn > current_bot_count) {
        G_AddBot(numBotsToSpawn - current_bot_count);
    } else if (numBotsToSpawn < current_bot_count) {
        G_RemoveBot(current_bot_count - numBotsToSpawn);
    }
}
