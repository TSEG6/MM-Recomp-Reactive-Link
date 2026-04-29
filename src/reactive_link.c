#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"


// Vars
bool shouldLook = false;
bool IsAnimBlacklisted(Player* player);
int CombatIdleCooldown;
int ItemCooldown;
Actor* gTatlHoverActor = NULL;
int sCombatIdleStopTimer = 0;

// Allowed animations for combat idle
static uintptr_t sAllowedBaseAnims[] = {
    0x0400DF18, 0x0400DF28, 0x0400E0B0, 0x0400DEE8,
    0x0400DF20, 0x0400E0E0, 0x0400E0F0, 0x0400DEF8,
    0x0400DF30, 0x0400E0D8, 0x0400E410, 0x0400E260
};

// Rotation Blacklist
static uintptr_t sHeadLookBlacklist[] = {
    0x0400DD28, 0x0400DF68, 0x0400E0B0,
    0x0400DEE8, 0x0400E0B8, 0x0400E0F0,
    0x0400E0E8, 0x0400E0E0, 0x0400DF30,
    0x0400E0D8, 0x0400D210, 0x0400D208,
    0x0400DDB8, 0x0400DDB0, 0x0400DB18,
    0x0400D5B0, 0x0400D128, 0x0400D5A8,
    0x0400CF98, 0x0400D0B0, 0x0400D4A0,
    0x0400E270, 0x0400D498, 0x0400D4D8,
    0x0400E2C0, 0x0400DBF0, 0x0400E2C8,
    0x0400DF68, 0x0400E400, 0x0400D0C8,
    0x0400D0D0, 0x0400D0C8

};


// Moved this over to just music checks instead of actor enemy checks, should be less buggy?
bool IsCombatMusicPlaying() {

    u16 currentBgm = AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);

    switch (currentBgm) {
    case NA_BGM_MAJORAS_LAIR:
    case NA_BGM_MAJORAS_MASK:
    case NA_BGM_MAJORAS_INCARNATION:
    case NA_BGM_MAJORAS_WRATH:
    case NA_BGM_MINI_BOSS:
    case NA_BGM_BOSS:
    case NA_BGM_CHASE:
    case NA_BGM_MAJORAS_THEME:
    case NA_BGM_ALIEN_INVASION:
        return true;
    default:
        return false;
    }
}

bool IsAnimBlacklisted(Player* player) {
    uintptr_t currentAnim = (uintptr_t)player->skelAnime.animation;
    int numBlacklisted = sizeof(sHeadLookBlacklist) / sizeof(sHeadLookBlacklist[0]);

    for (int i = 0; i < numBlacklisted; i++) {
        if (currentAnim == sHeadLookBlacklist[i]) return true;
    }
    return false;
}

RECOMP_HOOK("Player_Action_84")
void BetterLink_PlayerBButtonSword(Player* player, PlayState* play) {
    CombatIdleCooldown = 10;
}

RECOMP_HOOK("Actor_GetFocus")
void BetterLink_ActorGetFocus(Actor* actor) {
    ItemCooldown = 10;
}

RECOMP_HOOK("func_80840A30")
void BetterLink_Roll(Actor* actor) {
    ItemCooldown = 10;
}

RECOMP_HOOK("Player_Action_50")
void BetterLink_Climb(Actor* actor) {
    ItemCooldown = 10;
}

RECOMP_HOOK("Attention_Update")
void RLAU(Attention* attention, Player* player, Actor* playerFocusActor, PlayState* play) {
    gTatlHoverActor = attention->tatlHoverActor;
    shouldLook = (playerFocusActor != gTatlHoverActor);

    if (CombatIdleCooldown > 0) CombatIdleCooldown--;
    if (ItemCooldown > 0) ItemCooldown--;
}

// New Main Code

RECOMP_HOOK("Player_Update")
void BetterLink_UpdateRotation(Player* player, PlayState* play) {

    double battleStanceConfig = recomp_get_config_double("battle_stance");
    double lookStrength = recomp_get_config_double("look_strength");
    s32 camSetting = play->cameraPtrs[0]->setting;

    if (play->cameraPtrs[CAM_ID_MAIN]->mode == 20) {
        return;
    }

    if (camSetting == CAM_MODE_FIRSTPERSON || camSetting == CAM_MODE_BOWARROWZ ||
        camSetting == CAM_MODE_BOWARROW || camSetting == CAM_MODE_HOOKSHOT ||
        camSetting == CAM_MODE_BOOMERANG || camSetting == CAM_MODE_ZORAFIN ||
        camSetting == CAM_MODE_ZORAFINZ || camSetting == CAM_MODE_DEKUHIDE) {
        return;
    }

    // Head looking system, now with actual y axis working for NPC's

    if (ItemCooldown == 0 &&
        CombatIdleCooldown == 0 &&
        shouldLook &&
        gTatlHoverActor != NULL &&
        !IsAnimBlacklisted(player)) {

        float dx = gTatlHoverActor->focus.pos.x - player->actor.focus.pos.x;
        float dy = gTatlHoverActor->focus.pos.y - player->actor.focus.pos.y;
        float dz = gTatlHoverActor->focus.pos.z - player->actor.focus.pos.z;

        s16 rawYaw = Math_Atan2S(dx, dz) - player->actor.shape.rot.y;
        s16 rawPitch = -Math_Atan2S(dy, sqrtf(dx * dx + dz * dz));
        s16 targetYaw = (s16)(rawYaw * lookStrength);
        s16 targetPitch = (s16)(rawPitch * lookStrength);

        Math_SmoothStepToS(&player->headLimbRot.y, targetYaw, 4, 0x1000, 0x10);
        Math_SmoothStepToS(&player->headLimbRot.x, targetPitch, 4, 0x1000, 0x10);

        player->actor.focus.rot.x = player->headLimbRot.x;
        // doing this for y caused some weird issues but without it seems fine?

    }
    else {

        Math_SmoothStepToS(&player->headLimbRot.y, 0, 4, 0x1000, 0x10);
        Math_SmoothStepToS(&player->headLimbRot.x, 0, 4, 0x1000, 0x10);
    }

    // Combat Idle logic... totally didn't copy most of this from the original.
    if (battleStanceConfig == 0.0f) {

        bool isInCombat = IsCombatMusicPlaying() || (play->actorCtx.attention.bgmEnemy != NULL);

        PlayerAnimationHeader* combatAnim = (PlayerAnimationHeader*)0x0400DF18;

        if (CombatIdleCooldown == 0 && ItemCooldown == 0 && player->actor.speed == 0.0f && isInCombat) {
            bool canPlayCombatAnim = false;
            for (size_t i = 0; i < sizeof(sAllowedBaseAnims) / sizeof(sAllowedBaseAnims[0]); i++) {
                if ((uintptr_t)player->skelAnime.animation == sAllowedBaseAnims[i]) {
                    canPlayCombatAnim = true;
                    break;
                }
            }

            if (canPlayCombatAnim && (uintptr_t)player->skelAnime.animation != (uintptr_t)combatAnim) {
                f32 endFrame = Animation_GetLastFrame(combatAnim);
                PlayerAnimation_Change(play, &player->skelAnime, combatAnim, 0.5f, 0.0f, endFrame, ANIMMODE_LOOP, 8.0f);
                sCombatIdleStopTimer = 10;
            }
        }
        else {

            if ((uintptr_t)player->skelAnime.animation == (uintptr_t)combatAnim) {
                if (sCombatIdleStopTimer > 0) {
                    sCombatIdleStopTimer--;
                }
                else if (player->actor.speed == 0.0f) {
                    PlayerAnimationHeader* normalIdle = (PlayerAnimationHeader*)0x0400DF28;
                    f32 endFrame = Animation_GetLastFrame(normalIdle);
                    PlayerAnimation_Change(play, &player->skelAnime, normalIdle, 1.0f, 0.0f, endFrame, ANIMMODE_LOOP, 8.0f);
                }
            }
        }
    }
}