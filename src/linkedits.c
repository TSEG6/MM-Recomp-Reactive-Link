#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"

// Vars
static s16 sHeadYawTarget = 0;
static s16 sHeadPitchTarget = 0;
static bool sEnemyNearby = false;
static float sCurrentLookRange = 0.0f;
static Vec3f sLookPos = { 0, 0, 0 };
static PlayerAnimationHeader* sCurrentAnim = NULL;

// Cooldowns
static s32 sCombatIdleCooldown = 0;
static s32 sCombatIdleStopTimer = 0;
static s32 sItemCooldown = 0;

// Allowed animations for combat idle
static uintptr_t sAllowedBaseAnims[] = {
    0x0400DF18, 0x0400DF28, 0x0400E0B0, 0x0400DEE8,
    0x0400DF20, 0x0400E0E0, 0x0400E0F0, 0x0400DEF8,
    0x0400DF30, 0x0400E0D8, 0x0400E410, 0x0400E260
};

// Rotation Blacklist for NPCs
static uintptr_t sHeadLookBlacklist[] = {
    0x0400DD28,
    0x0400DF68,
    0x0400E0B0,
    0x0400DEE8,
    0x0400E0B8,
    0x0400E0F0,
    0x0400E0E8,
    0x0400E0E0,
    0x0400DF30,
    0x0400E0D8
};

// Enemy blacklist (actors Link should NOT look at)
static u16 gEnemyLookBlacklist[] = {
    573, // Moths

};

// Config Preset
static float gDefaultEnemyLookRange = 260.0f;
static float gDefaultEnemyExitRange = 300.0f;
static float gHighEnemyLookRange = 9999.0f;
static float gHighEnemyExitRange = 9999.0f;

// Boss BGM sequences
static u16 gBossBgmTracks[] = {
    NA_BGM_MAJORAS_WRATH,
    NA_BGM_MAJORAS_INCARNATION,
    NA_BGM_MAJORAS_MASK,
    NA_BGM_MINI_BOSS,
    NA_BGM_BOSS,
    NA_BGM_MAJORAS_LAIR
};

// Scenes to ignore NPC look
static u16 gIgnoreNpcLookScenes[] = {
    SCENE_00KEIKOKU
};

// NPC blacklist
static u16 gNpcBlacklist[] = {
    164, 514, 469
};

// Enemy rotation blacklist scenes
static u16 gEnemyHeadBlacklistScenes[] = {
    SCENE_LAST_BS,
    SCENE_MITURIN_BS,
    SCENE_INISIE_BS,
    SCENE_SEA_BS,
    SCENE_HAKUGIN_BS
};


// This is probably overdone?

static bool ShouldIgnoreNpcLook(u16 sceneId) {
    for (size_t i = 0; i < sizeof(gIgnoreNpcLookScenes) / sizeof(gIgnoreNpcLookScenes[0]); i++) {
        if (sceneId == gIgnoreNpcLookScenes[i]) return true;
    }
    return false;
}

static bool IsNpcBlacklisted(u16 actorId) {
    for (size_t i = 0; i < sizeof(gNpcBlacklist) / sizeof(gNpcBlacklist[0]); i++) {
        if (actorId == gNpcBlacklist[i]) return true;
    }
    return false;
}

static bool ShouldIgnoreEnemyHead(u16 sceneId) {
    for (size_t i = 0; i < sizeof(gEnemyHeadBlacklistScenes) / sizeof(gEnemyHeadBlacklistScenes[0]); i++) {
        if (sceneId == gEnemyHeadBlacklistScenes[i]) return true;
    }
    return false;
}

static bool IsEnemyBlacklisted(u16 actorId) {
    for (size_t i = 0; i < sizeof(gEnemyLookBlacklist) / sizeof(gEnemyLookBlacklist[0]); i++) {
        if (actorId == gEnemyLookBlacklist[i]) return true;
    }
    return false;
}



static void BetterLink_UpdateTargetPosition(PlayState* play, Player* player) {
    Vec3f nearestPos;
    float nearestDist = 999999.0f;
    bool foundEnemy = false;
    bool found = false;

    // Music Check
    u16 activeSeq = AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);
    bool bossBgmPlaying = false;
    for (size_t i = 0; i < sizeof(gBossBgmTracks) / sizeof(gBossBgmTracks[0]); i++) {
        if (activeSeq == gBossBgmTracks[i]) {
            bossBgmPlaying = true;
            break;
        }
    }

    float enemyLookRange = bossBgmPlaying ? gHighEnemyLookRange : gDefaultEnemyLookRange;
    float enemyExitRange = bossBgmPlaying ? gHighEnemyExitRange : gDefaultEnemyExitRange;

    bool ignoreNpcs = ShouldIgnoreNpcLook(play->sceneId);

    // NPC's
    if (!ignoreNpcs) {
        for (Actor* actor = play->actorCtx.actorLists[ACTORCAT_NPC].first; actor; actor = actor->next) {
            if (!actor->update || IsNpcBlacklisted(actor->id)) continue;

            Vec3f targetPos;
            targetPos.x = actor->world.pos.x;
            targetPos.z = actor->world.pos.z;
            targetPos.y = actor->world.pos.y + actor->shape.yOffset + 20.0f;

            float dx = targetPos.x - player->actor.world.pos.x;
            float dy = targetPos.y - (player->actor.world.pos.y + 60.0f);
            float dz = targetPos.z - player->actor.world.pos.z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);

            if (dist < 160.0f && dist < nearestDist) {
                nearestPos = targetPos;
                nearestDist = dist;
                foundEnemy = false;
                found = true;
            }
        }
    }

    // Enemies
    if (!bossBgmPlaying) {
        for (Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first; actor; actor = actor->next) {
            if (!actor->update || actor->colChkInfo.health <= 0) continue;
            if (IsEnemyBlacklisted(actor->id)) continue;

            Vec3f focus = actor->focus.pos;
            float dx = focus.x - player->actor.world.pos.x;
            float dy = focus.y - (player->actor.world.pos.y + 60.0f);
            float dz = focus.z - player->actor.world.pos.z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);

            if (dist < enemyLookRange && dist < nearestDist) {
                nearestPos = focus;
                nearestDist = dist;
                foundEnemy = true;
                found = true;
            }
        }
    }

    if (found) {
        sLookPos = nearestPos;
        sCurrentLookRange = enemyLookRange;
        sEnemyNearby = (!bossBgmPlaying) && foundEnemy && nearestDist < enemyExitRange;
    }
    else {
        sCurrentLookRange = 0.0f;
        sEnemyNearby = false;
    }
}


// Sword Cooldown
RECOMP_HOOK("Player_Action_84")
void BetterLink_PlayerBButtonSword(Player* player, PlayState* play) {
    sCombatIdleCooldown = 10;
}

// Item use cooldown
RECOMP_HOOK("Actor_GetFocus")
void BetterLink_ActorGetFocus(Actor* actor) {
    sItemCooldown = 10;
}

// Roll use cooldown
RECOMP_HOOK("func_80840A30")
void BetterLink_Roll(Actor* actor) {
    sItemCooldown = 10;
}

// The Hook with all the logic pretty much
RECOMP_HOOK("Player_Update")
void BetterLink_PlayerUpdate(Player* player, PlayState* play) {
    if (play->csCtx.state != CS_STATE_IDLE) return;

    if (sCombatIdleCooldown > 0) sCombatIdleCooldown--;
    if (sItemCooldown > 0) sItemCooldown--;

    s32 camSetting = play->cameraPtrs[0]->setting;
    bool canLook = true;
    if (camSetting == CAM_MODE_FIRSTPERSON ||
        camSetting == CAM_MODE_BOWARROWZ ||
        camSetting == CAM_MODE_BOWARROW ||
        camSetting == CAM_MODE_HOOKSHOT ||
        camSetting == CAM_MODE_BOOMERANG ||
        camSetting == CAM_MODE_ZORAFIN ||
        camSetting == CAM_MODE_ZORAFINZ ||
        camSetting == CAM_MODE_CHARGE ||
        camSetting == CAM_MODE_CLIMB ||
        camSetting == CAM_MODE_DEKUHIDE ||
        sItemCooldown > 0) {
        canLook = false;
    } // I'm going to be honest I don't think this does anything lol

    if (canLook) {
        BetterLink_UpdateTargetPosition(play, player);

        // Head ROT
        bool canRotateHeadNpc = true;
        double combatstance = recomp_get_config_double("battle_stance");
        for (size_t i = 0; i < sizeof(sHeadLookBlacklist) / sizeof(sHeadLookBlacklist[0]); i++) {
            if ((uintptr_t)player->skelAnime.animation == sHeadLookBlacklist[i]) {
                canRotateHeadNpc = false;
                break;
            }
        }

        bool canRotateHeadEnemy = !ShouldIgnoreEnemyHead(play->sceneId);

        bool canRotateHead = true;
        if (!canRotateHeadNpc) canRotateHead = false;
        if (!canRotateHeadEnemy && sEnemyNearby) canRotateHead = false;

        if (sCurrentLookRange > 0.0f && canRotateHead) {
            Vec3f diff;
            diff.x = sLookPos.x - player->actor.world.pos.x;
            diff.y = sLookPos.y - (player->actor.world.pos.y + 60.0f);
            diff.z = sLookPos.z - player->actor.world.pos.z;

            sHeadYawTarget = Math_Atan2S(diff.x, diff.z) - player->actor.shape.rot.y;
            sHeadPitchTarget = Math_Atan2S(diff.y, sqrtf(diff.x * diff.x + diff.z * diff.z));

            Math_SmoothStepToS(&player->headLimbRot.y, sHeadYawTarget, 5, 0x600, 0);
            Math_SmoothStepToS(&player->headLimbRot.x, sHeadPitchTarget, 5, 0x600, 0);
            Math_SmoothStepToS(&player->upperLimbRot.y, sHeadYawTarget / 2, 5, 0x400, 0);
            if (sEnemyNearby) Math_SmoothStepToS(&player->upperLimbRot.y, sHeadYawTarget * 3 / 4, 5, 0x600, 0);
        }
        else {
            sHeadYawTarget = 0;
            sHeadPitchTarget = 0;
        }

        // Animation handling
        bool bossBgmPlaying = false;
        u16 activeSeq = AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);
        for (size_t i = 0; i < sizeof(gBossBgmTracks) / sizeof(gBossBgmTracks[0]); i++) {
            if (activeSeq == gBossBgmTracks[i]) {
                bossBgmPlaying = true;
                break;
            }
        }

        if (combatstance == 0.0f) {
            if (sCombatIdleCooldown == 0 && player->speedXZ == 0.0f && (sEnemyNearby || bossBgmPlaying)) {
                bool canPlayCombatAnim = false;
                for (size_t i = 0; i < sizeof(sAllowedBaseAnims) / sizeof(sAllowedBaseAnims[0]); i++) {
                    if ((uintptr_t)player->skelAnime.animation == sAllowedBaseAnims[i]) {
                        canPlayCombatAnim = true;
                        break;
                    }
                }
                if (canPlayCombatAnim) {
                    PlayerAnimationHeader* combatAnim = (PlayerAnimationHeader*)0x0400DF18;
                    if ((uintptr_t)player->skelAnime.animation != (uintptr_t)combatAnim) {
                        f32 endFrame = Animation_GetLastFrame(combatAnim);
                        PlayerAnimation_Change(play, &player->skelAnime, combatAnim, 0.5f, 0.0f, endFrame, ANIMMODE_LOOP, 8.0f);
                        sCombatIdleStopTimer = 10;
                    }
                }
            }
            else {
                PlayerAnimationHeader* combatAnim = (PlayerAnimationHeader*)0x0400DF18;
                if ((uintptr_t)player->skelAnime.animation == (uintptr_t)combatAnim) {
                    if (sCombatIdleStopTimer > 0) {
                        sCombatIdleStopTimer--;
                    }
                    else {

                        if (player->speedXZ == 0.0f) {
                            PlayerAnimationHeader* normalIdle = (PlayerAnimationHeader*)0x0400DF28;
                            f32 endFrame = Animation_GetLastFrame(normalIdle);
                            PlayerAnimation_Change(play, &player->skelAnime, normalIdle, 1.0f, 0.0f, endFrame, ANIMMODE_LOOP, 8.0f);
                        }
                    }
                }
            }
        }
        else {
            PlayerAnimationHeader* combatAnim = (PlayerAnimationHeader*)0x0400DF18;
            if ((uintptr_t)player->skelAnime.animation == (uintptr_t)combatAnim) {
                if (sCombatIdleStopTimer > 0) {
                    sCombatIdleStopTimer--;
                }
                else {

                    if (player->speedXZ == 0.0f) {
                        PlayerAnimationHeader* normalIdle = (PlayerAnimationHeader*)0x0400DF28;
                        f32 endFrame = Animation_GetLastFrame(normalIdle);
                        PlayerAnimation_Change(play, &player->skelAnime, normalIdle, 1.0f, 0.0f, endFrame, ANIMMODE_LOOP, 8.0f);
                    }
                }
            }
        }
    }
}
