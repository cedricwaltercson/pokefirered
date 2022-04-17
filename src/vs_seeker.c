#include "global.h"
#include "gflib.h"
#include "constants/songs.h"
#include "task.h"
#include "event_object_movement.h"
#include "new_menu_helpers.h"
#include "item_use.h"
#include "event_scripts.h"
#include "event_data.h"
#include "script.h"
#include "event_object_lock.h"
#include "field_specials.h"
#include "item.h"
#include "item_menu.h"
#include "field_effect.h"
#include "script_movement.h"
#include "battle.h"
#include "battle_setup.h"
#include "random.h"
#include "field_player_avatar.h"
#include "vs_seeker.h"
#include "constants/event_object_movement.h"
#include "constants/event_objects.h"
#include "constants/maps.h"
#include "constants/items.h"
#include "constants/quest_log.h"
#include "constants/trainer_types.h"

enum
{
   VSSEEKER_NOT_CHARGED,
   VSSEEKER_NO_ONE_IN_RANGE,
   VSSEEKER_CAN_USE,
};

typedef enum
{
    VSSEEKER_SINGLE_RESP_RAND,
    VSSEEKER_SINGLE_RESP_NO,
    VSSEEKER_SINGLE_RESP_YES
} VsSeekerSingleRespCode;

typedef enum
{
    VSSEEKER_RESPONSE_NO_RESPONSE,
    VSSEEKER_RESPONSE_UNFOUGHT_TRAINERS,
    VSSEEKER_RESPONSE_FOUND_REMATCHES
} VsSeekerResponseCode;

// static types
typedef struct VsSeekerData
{
    u16 trainerIdxs[6];
    u16 mapGroup; // unused
    u16 mapNum; // unused
} VsSeekerData;

struct VsSeekerTrainerInfo
{
    const u8 *script;
    u16 trainerIdx;
    u8 localId;
    u8 objectEventId;
    s16 xCoord;
    s16 yCoord;
    u8 graphicsId;
};

struct VsSeekerStruct
{
    /*0x000*/ struct VsSeekerTrainerInfo trainerInfo[OBJECT_EVENTS_COUNT];
    /*0x100*/ u8 filler_100[0x300];
    /*0x400*/ u16 trainerIdxArray[OBJECT_EVENTS_COUNT];
    /*0x420*/ u8 runningBehaviourEtcArray[OBJECT_EVENTS_COUNT];
    /*0x430*/ u8 numRematchableTrainers;
    /*0x431*/ u8 trainerHasNotYetBeenFought:1;
    /*0x431*/ u8 trainerDoesNotWantRematch:1;
    /*0x431*/ u8 trainerWantsRematch:1;
    u8 responseCode:5;
};

// static declarations
static EWRAM_DATA struct VsSeekerStruct *sVsSeeker = NULL;

static void VsSeekerResetInBagStepCounter(void);
static void VsSeekerResetChargingStepCounter(void);
static void Task_ResetObjectsRematchWantedState(u8 taskId);
static void ResetMovementOfRematchableTrainers(void);
static void Task_VsSeeker_1(u8 taskId);
static void Task_VsSeeker_2(u8 taskId);
static void GatherNearbyTrainerInfo(void);
static void Task_VsSeeker_3(u8 taskId);
static bool8 CanUseVsSeeker(void);
static u8 GetVsSeekerResponseInArea(const VsSeekerData * vsSeekerData);
static u8 GetRematchTrainerIdGivenGameState(const u16 *trainerIdxs, u8 rematchIdx);
static u8 ShouldTryRematchBattleInternal(const VsSeekerData * vsSeekerData, u16 trainerBattleOpponent);
static u8 HasRematchTrainerAlreadyBeenFought(const VsSeekerData * vsSeekerData, u16 trainerBattleOpponent);
static int LookupVsSeekerOpponentInArray(const VsSeekerData * array, u16 trainerId);
static bool8 IsTrainerReadyForRematchInternal(const VsSeekerData * array, u16 trainerIdx);
static u8 GetRunningBehaviorFromGraphicsId(u8 graphicsId);
static u16 GetTrainerFlagFromScript(const u8 * script);
static int GetRematchIdx(const VsSeekerData * vsSeekerData, u16 trainerFlagIdx);
static bool32 IsThisTrainerRematchable(u32 localId);
static void ClearAllTrainerRematchStates(void);
static bool8 IsTrainerVisibleOnScreen(struct VsSeekerTrainerInfo * trainerInfo);
static u8 GetNextAvailableRematchTrainer(const VsSeekerData * vsSeekerData, u16 trainerFlagNo, u8 * idxPtr);
static u8 GetRematchableTrainerLocalId(void);
static void StartTrainerObjectMovementScript(struct VsSeekerTrainerInfo * trainerInfo, const u8 * script);
static u8 GetCurVsSeekerResponse(s32 vsSeekerIdx, u16 trainerIdx);
static void StartAllRespondantIdleMovements(void);
static bool8 ObjectEventIdIsSane(u8 objectEventId);
static u8 GetRandomFaceDirectionMovementType();

// rodata
static const VsSeekerData sVsSeekerData[] = {
   { {TRAINER_YOUNGSTER_BEN, TRAINER_YOUNGSTER_BEN_2, 0xFFFF, TRAINER_YOUNGSTER_BEN_3, TRAINER_YOUNGSTER_BEN_4},
      MAP_GROUP(ROUTE3), MAP_NUM(ROUTE3) },
   { {TRAINER_YOUNGSTER_CALVIN, TRAINER_YOUNGSTER_CALVIN},
      MAP_GROUP(ROUTE3), MAP_NUM(ROUTE3) },
   { {TRAINER_BUG_CATCHER_COLTON, TRAINER_BUG_CATCHER_COLTON_2, 0xFFFF, TRAINER_BUG_CATCHER_COLTON_3, 0xFFFF, TRAINER_BUG_CATCHER_COLTON_4},
      MAP_GROUP(ROUTE3), MAP_NUM(ROUTE3) },
   { {TRAINER_BUG_CATCHER_GREG, TRAINER_BUG_CATCHER_GREG},
      MAP_GROUP(ROUTE3), MAP_NUM(ROUTE3) },
   { {TRAINER_BUG_CATCHER_JAMES, TRAINER_BUG_CATCHER_JAMES},
      MAP_GROUP(ROUTE3), MAP_NUM(ROUTE3) },
   { {TRAINER_LASS_JANICE, TRAINER_LASS_JANICE_2, 0xFFFF, TRAINER_LASS_JANICE_3},
      MAP_GROUP(ROUTE3), MAP_NUM(ROUTE3) },
   { {TRAINER_LASS_SALLY, TRAINER_LASS_SALLY},
      MAP_GROUP(ROUTE3), MAP_NUM(ROUTE3) },
   { {TRAINER_LASS_ROBIN, TRAINER_LASS_ROBIN},
      MAP_GROUP(ROUTE3), MAP_NUM(ROUTE3) },
   { {TRAINER_LASS_CRISSY, TRAINER_LASS_CRISSY},
      MAP_GROUP(ROUTE4), MAP_NUM(ROUTE4) },
   { {TRAINER_YOUNGSTER_TIMMY, TRAINER_YOUNGSTER_TIMMY_2, 0xFFFF, TRAINER_YOUNGSTER_TIMMY_3, 0xFFFF, TRAINER_YOUNGSTER_TIMMY_4},
      MAP_GROUP(ROUTE24), MAP_NUM(ROUTE24) },
   { {TRAINER_BUG_CATCHER_CALE, TRAINER_BUG_CATCHER_CALE},
      MAP_GROUP(ROUTE24), MAP_NUM(ROUTE24) },
   { {TRAINER_LASS_RELI, TRAINER_LASS_RELI_2, 0xFFFF, TRAINER_LASS_RELI_3},
      MAP_GROUP(ROUTE24), MAP_NUM(ROUTE24) },
   { {TRAINER_LASS_ALI, TRAINER_LASS_ALI},
      MAP_GROUP(ROUTE24), MAP_NUM(ROUTE24) },
   { {TRAINER_CAMPER_SHANE, TRAINER_CAMPER_SHANE},
      MAP_GROUP(ROUTE24), MAP_NUM(ROUTE24) },
   { {TRAINER_CAMPER_ETHAN, TRAINER_CAMPER_ETHAN},
      MAP_GROUP(ROUTE24), MAP_NUM(ROUTE24) },
   { {TRAINER_YOUNGSTER_JOEY, TRAINER_YOUNGSTER_JOEY},
      MAP_GROUP(ROUTE25), MAP_NUM(ROUTE25) },
   { {TRAINER_YOUNGSTER_DAN, TRAINER_YOUNGSTER_DAN},
      MAP_GROUP(ROUTE25), MAP_NUM(ROUTE25) },
   { {TRAINER_YOUNGSTER_CHAD, TRAINER_YOUNGSTER_CHAD_2, 0xFFFF, TRAINER_YOUNGSTER_CHAD_3, TRAINER_YOUNGSTER_CHAD_4},
      MAP_GROUP(ROUTE25), MAP_NUM(ROUTE25) },
   { {TRAINER_PICNICKER_KELSEY, TRAINER_PICNICKER_KELSEY_2, 0xFFFF, TRAINER_PICNICKER_KELSEY_3, TRAINER_PICNICKER_KELSEY_4},
      MAP_GROUP(ROUTE25), MAP_NUM(ROUTE25) },
   { {TRAINER_LASS_HALEY, TRAINER_LASS_HALEY},
      MAP_GROUP(ROUTE25), MAP_NUM(ROUTE25) },
   { {TRAINER_HIKER_FRANKLIN, 0xFFFF, TRAINER_HIKER_FRANKLIN_2},
      MAP_GROUP(ROUTE25), MAP_NUM(ROUTE25) },
   { {TRAINER_HIKER_NOB, TRAINER_HIKER_NOB},
      MAP_GROUP(ROUTE25), MAP_NUM(ROUTE25) },
   { {TRAINER_HIKER_WAYNE, TRAINER_HIKER_WAYNE},
      MAP_GROUP(ROUTE25), MAP_NUM(ROUTE25) },
   { {TRAINER_CAMPER_FLINT, TRAINER_CAMPER_FLINT},
      MAP_GROUP(ROUTE25), MAP_NUM(ROUTE25) },
   { {TRAINER_BUG_CATCHER_KEIGO, TRAINER_BUG_CATCHER_KEIGO},
      MAP_GROUP(ROUTE6), MAP_NUM(ROUTE6) },
   { {TRAINER_BUG_CATCHER_ELIJAH, TRAINER_BUG_CATCHER_ELIJAH},
      MAP_GROUP(ROUTE6), MAP_NUM(ROUTE6) },
   { {TRAINER_CAMPER_RICKY, TRAINER_CAMPER_RICKY_2, 0xFFFF, TRAINER_CAMPER_RICKY_3, 0xFFFF, TRAINER_CAMPER_RICKY_4},
      MAP_GROUP(ROUTE6), MAP_NUM(ROUTE6) },
   { {TRAINER_CAMPER_JEFF, TRAINER_CAMPER_JEFF_2, 0xFFFF, TRAINER_CAMPER_JEFF_3, 0xFFFF, TRAINER_CAMPER_JEFF_4},
      MAP_GROUP(ROUTE6), MAP_NUM(ROUTE6) },
   { {TRAINER_PICNICKER_NANCY, TRAINER_PICNICKER_NANCY},
      MAP_GROUP(ROUTE6), MAP_NUM(ROUTE6) },
   { {TRAINER_PICNICKER_ISABELLE, TRAINER_PICNICKER_ISABELLE_2, 0xFFFF, TRAINER_PICNICKER_ISABELLE_3, TRAINER_PICNICKER_ISABELLE_4},
      MAP_GROUP(ROUTE6), MAP_NUM(ROUTE6) },
   { {TRAINER_YOUNGSTER_EDDIE, TRAINER_YOUNGSTER_EDDIE},
      MAP_GROUP(ROUTE11), MAP_NUM(ROUTE11) },
   { {TRAINER_YOUNGSTER_DILLON, TRAINER_YOUNGSTER_DILLON},
      MAP_GROUP(ROUTE11), MAP_NUM(ROUTE11) },
   { {TRAINER_YOUNGSTER_YASU, 0xFFFF, TRAINER_YOUNGSTER_YASU_2, 0xFFFF, TRAINER_YOUNGSTER_YASU_3},
      MAP_GROUP(ROUTE11), MAP_NUM(ROUTE11) },
   { {TRAINER_YOUNGSTER_DAVE, TRAINER_YOUNGSTER_DAVE},
      MAP_GROUP(ROUTE11), MAP_NUM(ROUTE11) },
   { {TRAINER_ENGINEER_BRAXTON, TRAINER_ENGINEER_BRAXTON},
      MAP_GROUP(ROUTE11), MAP_NUM(ROUTE11) },
   { {TRAINER_ENGINEER_BERNIE, 0xFFFF, 0xFFFF, TRAINER_ENGINEER_BERNIE_2},
      MAP_GROUP(ROUTE11), MAP_NUM(ROUTE11) },
   { {TRAINER_GAMER_HUGO, TRAINER_GAMER_HUGO},
      MAP_GROUP(ROUTE11), MAP_NUM(ROUTE11) },
   { {TRAINER_GAMER_JASPER, TRAINER_GAMER_JASPER},
      MAP_GROUP(ROUTE11), MAP_NUM(ROUTE11) },
   { {TRAINER_GAMER_DIRK, TRAINER_GAMER_DIRK},
      MAP_GROUP(ROUTE11), MAP_NUM(ROUTE11) },
   { {TRAINER_GAMER_DARIAN, 0xFFFF, 0xFFFF, TRAINER_GAMER_DARIAN_2},
      MAP_GROUP(ROUTE11), MAP_NUM(ROUTE11) },
   { {TRAINER_BUG_CATCHER_BRENT, TRAINER_BUG_CATCHER_BRENT},
      MAP_GROUP(ROUTE9), MAP_NUM(ROUTE9) },
   { {TRAINER_BUG_CATCHER_CONNER, TRAINER_BUG_CATCHER_CONNER},
      MAP_GROUP(ROUTE9), MAP_NUM(ROUTE9) },
   { {TRAINER_CAMPER_CHRIS, 0xFFFF, TRAINER_CAMPER_CHRIS_2, TRAINER_CAMPER_CHRIS_3, 0xFFFF, TRAINER_CAMPER_CHRIS_4},
      MAP_GROUP(ROUTE9), MAP_NUM(ROUTE9) },
   { {TRAINER_CAMPER_DREW, TRAINER_CAMPER_DREW},
      MAP_GROUP(ROUTE9), MAP_NUM(ROUTE9) },
   { {TRAINER_PICNICKER_ALICIA, 0xFFFF, TRAINER_PICNICKER_ALICIA_2, TRAINER_PICNICKER_ALICIA_3, 0xFFFF, TRAINER_PICNICKER_ALICIA_4},
      MAP_GROUP(ROUTE9), MAP_NUM(ROUTE9) },
   { {TRAINER_PICNICKER_CAITLIN, TRAINER_PICNICKER_CAITLIN},
      MAP_GROUP(ROUTE9), MAP_NUM(ROUTE9) },
   { {TRAINER_HIKER_ALAN, TRAINER_HIKER_ALAN},
      MAP_GROUP(ROUTE9), MAP_NUM(ROUTE9) },
   { {TRAINER_HIKER_BRICE, TRAINER_HIKER_BRICE},
      MAP_GROUP(ROUTE9), MAP_NUM(ROUTE9) },
   { {TRAINER_HIKER_JEREMY, 0xFFFF, 0xFFFF, TRAINER_HIKER_JEREMY_2},
      MAP_GROUP(ROUTE9), MAP_NUM(ROUTE9) },
   { {TRAINER_PICNICKER_HEIDI, TRAINER_PICNICKER_HEIDI},
      MAP_GROUP(ROUTE10), MAP_NUM(ROUTE10) },
   { {TRAINER_PICNICKER_CAROL, TRAINER_PICNICKER_CAROL},
      MAP_GROUP(ROUTE10), MAP_NUM(ROUTE10) },
   { {TRAINER_POKEMANIAC_MARK, 0xFFFF, 0xFFFF, TRAINER_POKEMANIAC_MARK_2, 0xFFFF, TRAINER_POKEMANIAC_MARK_3},
      MAP_GROUP(ROUTE10), MAP_NUM(ROUTE10) },
   { {TRAINER_POKEMANIAC_HERMAN, 0xFFFF, 0xFFFF, TRAINER_POKEMANIAC_HERMAN_2, 0xFFFF, TRAINER_POKEMANIAC_HERMAN_3},
      MAP_GROUP(ROUTE10), MAP_NUM(ROUTE10) },
   { {TRAINER_HIKER_CLARK, TRAINER_HIKER_CLARK},
      MAP_GROUP(ROUTE10), MAP_NUM(ROUTE10) },
   { {TRAINER_HIKER_TRENT, 0xFFFF, 0xFFFF, TRAINER_HIKER_TRENT_2},
      MAP_GROUP(ROUTE10), MAP_NUM(ROUTE10) },
   { {TRAINER_LASS_PAIGE, TRAINER_LASS_PAIGE},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_LASS_ANDREA, TRAINER_LASS_ANDREA},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_LASS_MEGAN, 0xFFFF, TRAINER_LASS_MEGAN_2, 0xFFFF, TRAINER_LASS_MEGAN_3},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_LASS_JULIA, TRAINER_LASS_JULIA},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_SUPER_NERD_AIDAN, TRAINER_SUPER_NERD_AIDAN},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_SUPER_NERD_GLENN, 0xFFFF, 0xFFFF, TRAINER_SUPER_NERD_GLENN_2},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_SUPER_NERD_LESLIE, TRAINER_SUPER_NERD_LESLIE},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_GAMER_STAN, TRAINER_GAMER_STAN},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_GAMER_RICH, 0xFFFF, 0xFFFF, TRAINER_GAMER_RICH_2},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_TWINS_ELI_ANNE, 0xFFFF, 0xFFFF, TRAINER_TWINS_ELI_ANNE_2},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_BIKER_RICARDO, TRAINER_BIKER_RICARDO},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_BIKER_JAREN, 0xFFFF, 0xFFFF, TRAINER_BIKER_JAREN_2},
      MAP_GROUP(ROUTE8), MAP_NUM(ROUTE8) },
   { {TRAINER_FISHERMAN_NED, TRAINER_FISHERMAN_NED},
      MAP_GROUP(ROUTE12), MAP_NUM(ROUTE12) },
   { {TRAINER_FISHERMAN_CHIP, TRAINER_FISHERMAN_CHIP},
      MAP_GROUP(ROUTE12), MAP_NUM(ROUTE12) },
   { {TRAINER_FISHERMAN_HANK, TRAINER_FISHERMAN_HANK},
      MAP_GROUP(ROUTE12), MAP_NUM(ROUTE12) },
   { {TRAINER_FISHERMAN_ELLIOT, 0xFFFF, 0xFFFF, TRAINER_FISHERMAN_ELLIOT_2},
      MAP_GROUP(ROUTE12), MAP_NUM(ROUTE12) },
   { {TRAINER_FISHERMAN_ANDREW, TRAINER_FISHERMAN_ANDREW},
      MAP_GROUP(ROUTE12), MAP_NUM(ROUTE12) },
   { {TRAINER_ROCKER_LUCA, 0xFFFF, 0xFFFF, TRAINER_ROCKER_LUCA_2},
      MAP_GROUP(ROUTE12), MAP_NUM(ROUTE12) },
   { {TRAINER_CAMPER_JUSTIN, TRAINER_CAMPER_JUSTIN},
      MAP_GROUP(ROUTE12), MAP_NUM(ROUTE12) },
   { {TRAINER_YOUNG_COUPLE_GIA_JES, 0xFFFF, 0xFFFF, TRAINER_YOUNG_COUPLE_GIA_JES_2, 0xFFFF, TRAINER_YOUNG_COUPLE_GIA_JES_3},
      MAP_GROUP(ROUTE12), MAP_NUM(ROUTE12) },
   { {TRAINER_BIKER_JARED, TRAINER_BIKER_JARED},
      MAP_GROUP(ROUTE13), MAP_NUM(ROUTE13) },
   { {TRAINER_BEAUTY_LOLA, TRAINER_BEAUTY_LOLA},
      MAP_GROUP(ROUTE13), MAP_NUM(ROUTE13) },
   { {TRAINER_BEAUTY_SHEILA, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_BEAUTY_SHEILA_2},
      MAP_GROUP(ROUTE13), MAP_NUM(ROUTE13) },
   { {TRAINER_BIRD_KEEPER_SEBASTIAN, TRAINER_BIRD_KEEPER_SEBASTIAN},
      MAP_GROUP(ROUTE13), MAP_NUM(ROUTE13) },
   { {TRAINER_BIRD_KEEPER_PERRY, TRAINER_BIRD_KEEPER_PERRY},
      MAP_GROUP(ROUTE13), MAP_NUM(ROUTE13) },
   { {TRAINER_BIRD_KEEPER_ROBERT, 0xFFFF, 0xFFFF, TRAINER_BIRD_KEEPER_ROBERT_2, TRAINER_BIRD_KEEPER_ROBERT_3},
      MAP_GROUP(ROUTE13), MAP_NUM(ROUTE13) },
   { {TRAINER_PICNICKER_ALMA, TRAINER_PICNICKER_ALMA},
      MAP_GROUP(ROUTE13), MAP_NUM(ROUTE13) },
   { {TRAINER_PICNICKER_SUSIE, 0xFFFF, 0xFFFF, TRAINER_PICNICKER_SUSIE_2, TRAINER_PICNICKER_SUSIE_3, TRAINER_PICNICKER_SUSIE_4},
      MAP_GROUP(ROUTE13), MAP_NUM(ROUTE13) },
   { {TRAINER_PICNICKER_VALERIE, TRAINER_PICNICKER_VALERIE},
      MAP_GROUP(ROUTE13), MAP_NUM(ROUTE13) },
   { {TRAINER_PICNICKER_GWEN, TRAINER_PICNICKER_GWEN},
      MAP_GROUP(ROUTE13), MAP_NUM(ROUTE13) },
   { {TRAINER_BIKER_MALIK, TRAINER_BIKER_MALIK},
      MAP_GROUP(ROUTE14), MAP_NUM(ROUTE14) },
   { {TRAINER_BIKER_LUKAS, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_BIKER_LUKAS_2},
      MAP_GROUP(ROUTE14), MAP_NUM(ROUTE14) },
   { {TRAINER_BIKER_ISAAC, TRAINER_BIKER_ISAAC},
      MAP_GROUP(ROUTE14), MAP_NUM(ROUTE14) },
   { {TRAINER_BIKER_GERALD, TRAINER_BIKER_GERALD},
      MAP_GROUP(ROUTE14), MAP_NUM(ROUTE14) },
   { {TRAINER_BIRD_KEEPER_DONALD, TRAINER_BIRD_KEEPER_DONALD},
      MAP_GROUP(ROUTE14), MAP_NUM(ROUTE14) },
   { {TRAINER_BIRD_KEEPER_BENNY, 0xFFFF, 0xFFFF, TRAINER_BIRD_KEEPER_BENNY_2, TRAINER_BIRD_KEEPER_BENNY_3},
      MAP_GROUP(ROUTE14), MAP_NUM(ROUTE14) },
   { {TRAINER_BIRD_KEEPER_CARTER, TRAINER_BIRD_KEEPER_CARTER},
      MAP_GROUP(ROUTE14), MAP_NUM(ROUTE14) },
   { {TRAINER_BIRD_KEEPER_MITCH, TRAINER_BIRD_KEEPER_MITCH},
      MAP_GROUP(ROUTE14), MAP_NUM(ROUTE14) },
   { {TRAINER_BIRD_KEEPER_BECK, TRAINER_BIRD_KEEPER_BECK},
      MAP_GROUP(ROUTE14), MAP_NUM(ROUTE14) },
   { {TRAINER_BIRD_KEEPER_MARLON, 0xFFFF, 0xFFFF, TRAINER_BIRD_KEEPER_MARLON_2, TRAINER_BIRD_KEEPER_MARLON_3},
      MAP_GROUP(ROUTE14), MAP_NUM(ROUTE14) },
   { {TRAINER_TWINS_KIRI_JAN, TRAINER_TWINS_KIRI_JAN},
      MAP_GROUP(ROUTE14), MAP_NUM(ROUTE14) },
   { {TRAINER_BIKER_ERNEST, TRAINER_BIKER_ERNEST},
      MAP_GROUP(ROUTE15), MAP_NUM(ROUTE15) },
   { {TRAINER_BIKER_ALEX, TRAINER_BIKER_ALEX},
      MAP_GROUP(ROUTE15), MAP_NUM(ROUTE15) },
   { {TRAINER_BEAUTY_GRACE, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_BEAUTY_GRACE_2},
      MAP_GROUP(ROUTE15), MAP_NUM(ROUTE15) },
   { {TRAINER_BEAUTY_OLIVIA, TRAINER_BEAUTY_OLIVIA},
      MAP_GROUP(ROUTE15), MAP_NUM(ROUTE15) },
   { {TRAINER_BIRD_KEEPER_EDWIN, TRAINER_BIRD_KEEPER_EDWIN},
      MAP_GROUP(ROUTE15), MAP_NUM(ROUTE15) },
   { {TRAINER_BIRD_KEEPER_CHESTER, 0xFFFF, 0xFFFF, TRAINER_BIRD_KEEPER_CHESTER_2, TRAINER_BIRD_KEEPER_CHESTER_3},
      MAP_GROUP(ROUTE15), MAP_NUM(ROUTE15) },
   { {TRAINER_PICNICKER_YAZMIN, TRAINER_PICNICKER_YAZMIN},
      MAP_GROUP(ROUTE15), MAP_NUM(ROUTE15) },
   { {TRAINER_PICNICKER_KINDRA, TRAINER_PICNICKER_KINDRA},
      MAP_GROUP(ROUTE15), MAP_NUM(ROUTE15) },
   { {TRAINER_PICNICKER_BECKY, 0xFFFF, 0xFFFF, TRAINER_PICNICKER_BECKY_2, TRAINER_PICNICKER_BECKY_3, TRAINER_PICNICKER_BECKY_4},
      MAP_GROUP(ROUTE15), MAP_NUM(ROUTE15) },
   { {TRAINER_PICNICKER_CELIA, TRAINER_PICNICKER_CELIA},
      MAP_GROUP(ROUTE15), MAP_NUM(ROUTE15) },
   { {TRAINER_CRUSH_KIN_RON_MYA, 0xFFFF, 0xFFFF, TRAINER_CRUSH_KIN_RON_MYA_2, TRAINER_CRUSH_KIN_RON_MYA_3, TRAINER_CRUSH_KIN_RON_MYA_4},
      MAP_GROUP(ROUTE15), MAP_NUM(ROUTE15) },
   { {TRAINER_BIKER_LAO, TRAINER_BIKER_LAO},
      MAP_GROUP(ROUTE16), MAP_NUM(ROUTE16) },
   { {TRAINER_BIKER_HIDEO, TRAINER_BIKER_HIDEO},
      MAP_GROUP(ROUTE16), MAP_NUM(ROUTE16) },
   { {TRAINER_BIKER_RUBEN, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_BIKER_RUBEN_2},
      MAP_GROUP(ROUTE16), MAP_NUM(ROUTE16) },
   { {TRAINER_CUE_BALL_KOJI, TRAINER_CUE_BALL_KOJI},
      MAP_GROUP(ROUTE16), MAP_NUM(ROUTE16) },
   { {TRAINER_CUE_BALL_LUKE, TRAINER_CUE_BALL_LUKE},
      MAP_GROUP(ROUTE16), MAP_NUM(ROUTE16) },
   { {TRAINER_CUE_BALL_CAMRON, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_CUE_BALL_CAMRON_2},
      MAP_GROUP(ROUTE16), MAP_NUM(ROUTE16) },
   { {TRAINER_YOUNG_COUPLE_LEA_JED, TRAINER_YOUNG_COUPLE_LEA_JED},
      MAP_GROUP(ROUTE16), MAP_NUM(ROUTE16) },
   { {TRAINER_BIKER_BILLY, TRAINER_BIKER_BILLY},
      MAP_GROUP(ROUTE17), MAP_NUM(ROUTE17) },
   { {TRAINER_BIKER_NIKOLAS, TRAINER_BIKER_NIKOLAS},
      MAP_GROUP(ROUTE17), MAP_NUM(ROUTE17) },
   { {TRAINER_BIKER_JAXON, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_BIKER_JAXON_2},
      MAP_GROUP(ROUTE17), MAP_NUM(ROUTE17) },
   { {TRAINER_BIKER_WILLIAM, TRAINER_BIKER_WILLIAM},
      MAP_GROUP(ROUTE17), MAP_NUM(ROUTE17) },
   { {TRAINER_CUE_BALL_RAUL, TRAINER_CUE_BALL_RAUL},
      MAP_GROUP(ROUTE17), MAP_NUM(ROUTE17) },
   { {TRAINER_CUE_BALL_ISAIAH, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_CUE_BALL_ISAIAH_2},
      MAP_GROUP(ROUTE17), MAP_NUM(ROUTE17) },
   { {TRAINER_CUE_BALL_ZEEK, TRAINER_CUE_BALL_ZEEK},
      MAP_GROUP(ROUTE17), MAP_NUM(ROUTE17) },
   { {TRAINER_CUE_BALL_JAMAL, TRAINER_CUE_BALL_JAMAL},
      MAP_GROUP(ROUTE17), MAP_NUM(ROUTE17) },
   { {TRAINER_CUE_BALL_COREY, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_CUE_BALL_COREY_2},
      MAP_GROUP(ROUTE17), MAP_NUM(ROUTE17) },
   { {TRAINER_BIKER_VIRGIL, TRAINER_BIKER_VIRGIL},
      MAP_GROUP(ROUTE17), MAP_NUM(ROUTE17) },
   { {TRAINER_BIRD_KEEPER_WILTON, TRAINER_BIRD_KEEPER_WILTON},
      MAP_GROUP(ROUTE18), MAP_NUM(ROUTE18) },
   { {TRAINER_BIRD_KEEPER_RAMIRO, TRAINER_BIRD_KEEPER_RAMIRO},
      MAP_GROUP(ROUTE18), MAP_NUM(ROUTE18) },
   { {TRAINER_BIRD_KEEPER_JACOB, 0xFFFF, 0xFFFF, TRAINER_BIRD_KEEPER_JACOB_2, TRAINER_BIRD_KEEPER_JACOB_3},
      MAP_GROUP(ROUTE18), MAP_NUM(ROUTE18) },
   { {TRAINER_SWIMMER_MALE_RICHARD, TRAINER_SWIMMER_MALE_RICHARD},
      MAP_GROUP(ROUTE19), MAP_NUM(ROUTE19) },
   { {TRAINER_SWIMMER_MALE_REECE, TRAINER_SWIMMER_MALE_REECE},
      MAP_GROUP(ROUTE19), MAP_NUM(ROUTE19) },
   { {TRAINER_SWIMMER_MALE_MATTHEW, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_SWIMMER_MALE_MATTHEW_2},
      MAP_GROUP(ROUTE19), MAP_NUM(ROUTE19) },
   { {TRAINER_SWIMMER_MALE_DOUGLAS, TRAINER_SWIMMER_MALE_DOUGLAS},
      MAP_GROUP(ROUTE19), MAP_NUM(ROUTE19) },
   { {TRAINER_SWIMMER_MALE_DAVID, TRAINER_SWIMMER_MALE_DAVID},
      MAP_GROUP(ROUTE19), MAP_NUM(ROUTE19) },
   { {TRAINER_SWIMMER_MALE_TONY, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_SWIMMER_MALE_TONY_2},
      MAP_GROUP(ROUTE19), MAP_NUM(ROUTE19) },
   { {TRAINER_SWIMMER_MALE_AXLE, TRAINER_SWIMMER_MALE_AXLE},
      MAP_GROUP(ROUTE19), MAP_NUM(ROUTE19) },
   { {TRAINER_SWIMMER_FEMALE_ANYA, TRAINER_SWIMMER_FEMALE_ANYA},
      MAP_GROUP(ROUTE19), MAP_NUM(ROUTE19) },
   { {TRAINER_SWIMMER_FEMALE_ALICE, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_SWIMMER_FEMALE_ALICE_2},
      MAP_GROUP(ROUTE19), MAP_NUM(ROUTE19) },
   { {TRAINER_SWIMMER_FEMALE_CONNIE, TRAINER_SWIMMER_FEMALE_CONNIE},
      MAP_GROUP(ROUTE19), MAP_NUM(ROUTE19) },
   { {TRAINER_SIS_AND_BRO_LIA_LUC, TRAINER_SIS_AND_BRO_LIA_LUC},
      MAP_GROUP(ROUTE19), MAP_NUM(ROUTE19) },
   { {TRAINER_SWIMMER_MALE_BARRY, TRAINER_SWIMMER_MALE_BARRY},
      MAP_GROUP(ROUTE20), MAP_NUM(ROUTE20) },
   { {TRAINER_SWIMMER_MALE_DEAN, TRAINER_SWIMMER_MALE_DEAN},
      MAP_GROUP(ROUTE20), MAP_NUM(ROUTE20) },
   { {TRAINER_SWIMMER_MALE_DARRIN, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_SWIMMER_MALE_DARRIN_2},
      MAP_GROUP(ROUTE20), MAP_NUM(ROUTE20) },
   { {TRAINER_SWIMMER_FEMALE_TIFFANY, TRAINER_SWIMMER_FEMALE_TIFFANY},
      MAP_GROUP(ROUTE20), MAP_NUM(ROUTE20) },
   { {TRAINER_SWIMMER_FEMALE_NORA, TRAINER_SWIMMER_FEMALE_NORA},
      MAP_GROUP(ROUTE20), MAP_NUM(ROUTE20) },
   { {TRAINER_SWIMMER_FEMALE_MELISSA, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_SWIMMER_FEMALE_MELISSA_2},
      MAP_GROUP(ROUTE20), MAP_NUM(ROUTE20) },
   { {TRAINER_SWIMMER_FEMALE_SHIRLEY, TRAINER_SWIMMER_FEMALE_SHIRLEY},
      MAP_GROUP(ROUTE20), MAP_NUM(ROUTE20) },
   { {TRAINER_BIRD_KEEPER_ROGER, TRAINER_BIRD_KEEPER_ROGER},
      MAP_GROUP(ROUTE20), MAP_NUM(ROUTE20) },
   { {TRAINER_PICNICKER_MISSY, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_PICNICKER_MISSY_2, TRAINER_PICNICKER_MISSY_3},
      MAP_GROUP(ROUTE20), MAP_NUM(ROUTE20) },
   { {TRAINER_PICNICKER_IRENE, TRAINER_PICNICKER_IRENE},
      MAP_GROUP(ROUTE20), MAP_NUM(ROUTE20) },
   { {TRAINER_FISHERMAN_RONALD, TRAINER_FISHERMAN_RONALD},
      MAP_GROUP(ROUTE21_NORTH), MAP_NUM(ROUTE21_NORTH) },
   { {TRAINER_FISHERMAN_CLAUDE, TRAINER_FISHERMAN_CLAUDE},
      MAP_GROUP(ROUTE21_NORTH), MAP_NUM(ROUTE21_NORTH) },
   { {TRAINER_FISHERMAN_WADE, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_FISHERMAN_WADE_2},
      MAP_GROUP(ROUTE21_NORTH), MAP_NUM(ROUTE21_NORTH) },
   { {TRAINER_FISHERMAN_NOLAN, TRAINER_FISHERMAN_NOLAN},
      MAP_GROUP(ROUTE21_NORTH), MAP_NUM(ROUTE21_NORTH) },
   { {TRAINER_SWIMMER_MALE_SPENCER, TRAINER_SWIMMER_MALE_SPENCER},
      MAP_GROUP(ROUTE21_NORTH), MAP_NUM(ROUTE21_NORTH) },
   { {TRAINER_SWIMMER_MALE_JACK, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_SWIMMER_MALE_JACK_2},
      MAP_GROUP(ROUTE21_NORTH), MAP_NUM(ROUTE21_NORTH) },
   { {TRAINER_SWIMMER_MALE_JEROME, TRAINER_SWIMMER_MALE_JEROME},
      MAP_GROUP(ROUTE21_NORTH), MAP_NUM(ROUTE21_NORTH) },
   { {TRAINER_SWIMMER_MALE_ROLAND, TRAINER_SWIMMER_MALE_ROLAND},
      MAP_GROUP(ROUTE21_NORTH), MAP_NUM(ROUTE21_NORTH) },
   { {TRAINER_SIS_AND_BRO_LIL_IAN, 0xFFFF, 0xFFFF, 0xFFFF, TRAINER_SIS_AND_BRO_LIL_IAN_2, TRAINER_SIS_AND_BRO_LIL_IAN_3},
      MAP_GROUP(ROUTE21_NORTH), MAP_NUM(ROUTE21_NORTH) },
};

static const u8 sMovementScript_Wait48[] = {
    MOVEMENT_ACTION_DELAY_16,
    MOVEMENT_ACTION_DELAY_16,
    MOVEMENT_ACTION_DELAY_16,
    MOVEMENT_ACTION_STEP_END
};

static const u8 sMovementScript_TrainerUnfought[] = {
    MOVEMENT_ACTION_EMOTE_EXCLAMATION_MARK,
    MOVEMENT_ACTION_STEP_END
};

static const u8 sMovementScript_TrainerNoRematch[] = {
    MOVEMENT_ACTION_EMOTE_X,
    MOVEMENT_ACTION_STEP_END
};

static const u8 sMovementScript_TrainerRematch[] = {
    MOVEMENT_ACTION_WALK_IN_PLACE_FASTEST_DOWN,
    MOVEMENT_ACTION_EMOTE_DOUBLE_EXCL_MARK,
    MOVEMENT_ACTION_STEP_END
};

static const u8 sFaceDirectionMovementTypeByFacingDirection[] = {
    MOVEMENT_TYPE_FACE_DOWN,
    MOVEMENT_TYPE_FACE_DOWN,
    MOVEMENT_TYPE_FACE_UP,
    MOVEMENT_TYPE_FACE_LEFT,
    MOVEMENT_TYPE_FACE_RIGHT
};

// text

void VsSeekerFreezeObjectsAfterChargeComplete(void)
{
    CreateTask(Task_ResetObjectsRematchWantedState, 80);
}

static void Task_ResetObjectsRematchWantedState(u8 taskId)
{
    struct Task * task = &gTasks[taskId];
    u8 i;

    if (task->data[0] == 0 && walkrun_is_standing_still() == TRUE)
    {
        HandleEnforcedLookDirectionOnPlayerStopMoving();
        task->data[0] = 1;
    }

    if (task->data[1] == 0)
    {
        for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
        {
            if (ObjectEventIdIsSane(i) == TRUE)
            {
                if (gObjectEvents[i].singleMovementActive)
                    return;
                FreezeObjectEvent(&gObjectEvents[i]);
            }
        }
    }

    task->data[1] = 1;
    if (task->data[0] != 0)
    {
        DestroyTask(taskId);
        StopPlayerAvatar();
        EnableBothScriptContexts();
    }
}

void VsSeekerResetObjectMovementAfterChargeComplete(void)
{
    struct ObjectEventTemplate * templates = gSaveBlock1Ptr->objectEventTemplates;
    u8 i;
    u8 movementType;
    u8 objEventId;
    struct ObjectEvent * objectEvent;

    for (i = 0; i < gMapHeader.events->objectEventCount; i++)
    {
        if ((templates[i].objUnion.normal.trainerType == TRAINER_TYPE_NORMAL
          || templates[i].objUnion.normal.trainerType == TRAINER_TYPE_BURIED) 
         && (templates[i].objUnion.normal.movementType == MOVEMENT_TYPE_VS_SEEKER_4D
          || templates[i].objUnion.normal.movementType == MOVEMENT_TYPE_VS_SEEKER_4E
          || templates[i].objUnion.normal.movementType == MOVEMENT_TYPE_VS_SEEKER_4F))
        {
            movementType = GetRandomFaceDirectionMovementType();
            TryGetObjectEventIdByLocalIdAndMap(templates[i].localId, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup, &objEventId);
            objectEvent = &gObjectEvents[objEventId];
            if (ObjectEventIdIsSane(objEventId) == TRUE)
            {
                SetTrainerMovementType(objectEvent, movementType);
            }
            templates[i].objUnion.normal.movementType = movementType;
        }
    }
}

bool8 UpdateVsSeekerStepCounter(void)
{
    u8 x = 0;

    if (CheckBagHasItem(ITEM_VS_SEEKER, 1) == TRUE)
    {
        if ((gSaveBlock1Ptr->trainerRematchStepCounter & 0xFF) < 100)
            gSaveBlock1Ptr->trainerRematchStepCounter++;
    }

    if (FlagGet(FLAG_SYS_VS_SEEKER_CHARGING) == TRUE)
    {
        if (((gSaveBlock1Ptr->trainerRematchStepCounter >> 8) & 0xFF) < 100)
        {
            x = (((gSaveBlock1Ptr->trainerRematchStepCounter >> 8) & 0xFF) + 1);
            gSaveBlock1Ptr->trainerRematchStepCounter = (gSaveBlock1Ptr->trainerRematchStepCounter & 0xFF) | (x << 8);
        }
        if (((gSaveBlock1Ptr->trainerRematchStepCounter >> 8) & 0xFF) == 100)
        {
            FlagClear(FLAG_SYS_VS_SEEKER_CHARGING);
            VsSeekerResetChargingStepCounter();
            ClearAllTrainerRematchStates();
            return TRUE;
        }
    }

    return FALSE;
}

void MapResetTrainerRematches(u16 mapGroup, u16 mapNum)
{
    FlagClear(FLAG_SYS_VS_SEEKER_CHARGING);
    VsSeekerResetChargingStepCounter();
    ClearAllTrainerRematchStates();
    ResetMovementOfRematchableTrainers();
}

static void ResetMovementOfRematchableTrainers(void)
{
    u8 i;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        struct ObjectEvent * objectEvent = &gObjectEvents[i];
        if (objectEvent->movementType == MOVEMENT_TYPE_VS_SEEKER_4D || objectEvent->movementType == MOVEMENT_TYPE_VS_SEEKER_4E || objectEvent->movementType == MOVEMENT_TYPE_VS_SEEKER_4F)
        {
            u8 movementType = GetRandomFaceDirectionMovementType();
            if (objectEvent->active && gSprites[objectEvent->spriteId].data[0] == i)
            {
                gSprites[objectEvent->spriteId].x2 = 0;
                gSprites[objectEvent->spriteId].y2 = 0;
                SetTrainerMovementType(objectEvent, movementType);
            }
        }
    }
}

static void VsSeekerResetInBagStepCounter(void)
{
    gSaveBlock1Ptr->trainerRematchStepCounter &= 0xFF00;
}

static void VsSeekerSetStepCounterInBagFull(void)
{
    gSaveBlock1Ptr->trainerRematchStepCounter &= 0xFF00;
    gSaveBlock1Ptr->trainerRematchStepCounter |= 100;
}

static void VsSeekerResetChargingStepCounter(void)
{
    gSaveBlock1Ptr->trainerRematchStepCounter &= 0x00FF;
}

static void VsSeekerSetStepCounterFullyCharged(void)
{
    gSaveBlock1Ptr->trainerRematchStepCounter &= 0x00FF;
    gSaveBlock1Ptr->trainerRematchStepCounter |= (100 << 8);
}

void Task_VsSeeker_0(u8 taskId)
{
    u8 i;
    u8 respval;

    for (i = 0; i < 16; i++)
        gTasks[taskId].data[i] = 0;

    sVsSeeker = AllocZeroed(sizeof(struct VsSeekerStruct));
    GatherNearbyTrainerInfo();
    respval = CanUseVsSeeker();
    if (respval == VSSEEKER_NOT_CHARGED)
    {
        Free(sVsSeeker);
        DisplayItemMessageOnField(taskId, 2, VSSeeker_Text_BatteryNotChargedNeedXSteps, Task_ItemUse_CloseMessageBoxAndReturnToField_VsSeeker);
    }
    else if (respval == VSSEEKER_NO_ONE_IN_RANGE)
    {
        Free(sVsSeeker);
        DisplayItemMessageOnField(taskId, 2, VSSeeker_Text_NoTrainersWithinRange, Task_ItemUse_CloseMessageBoxAndReturnToField_VsSeeker);
    }
    else if (respval == VSSEEKER_CAN_USE)
    {
        ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, 0, gSpecialVar_ItemId, 0xffff);
        FieldEffectStart(FLDEFF_USE_VS_SEEKER);
        gTasks[taskId].func = Task_VsSeeker_1;
        gTasks[taskId].data[0] = 15;
    }
}

static void Task_VsSeeker_1(u8 taskId)
{
    if (--gTasks[taskId].data[0] == 0)
    {
        gTasks[taskId].func = Task_VsSeeker_2;
        gTasks[taskId].data[1] = 16;
    }
}

static void Task_VsSeeker_2(u8 taskId)
{
    s16 * data = gTasks[taskId].data;

    if (data[2] != 2 && --data[1] == 0)
    {
        PlaySE(SE_CONTEST_MONS_TURN);
        data[1] = 11;
        data[2]++;
    }

    if (!FieldEffectActiveListContains(FLDEFF_USE_VS_SEEKER))
    {
        data[1] = 0;
        data[2] = 0;
        VsSeekerResetInBagStepCounter();
        sVsSeeker->responseCode = GetVsSeekerResponseInArea(sVsSeekerData);
        ScriptMovement_StartObjectMovementScript(0xFF, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup, sMovementScript_Wait48);
        gTasks[taskId].func = Task_VsSeeker_3;
    }
}

static void GatherNearbyTrainerInfo(void)
{
    struct ObjectEventTemplate *templates = gSaveBlock1Ptr->objectEventTemplates;
    u8 objectEventId = 0;
    u8 vsSeekerObjectIdx = 0;
    s32 objectEventIdx;

    for (objectEventIdx = 0; objectEventIdx < gMapHeader.events->objectEventCount; objectEventIdx++)
    {
        if (templates[objectEventIdx].objUnion.normal.trainerType == TRAINER_TYPE_NORMAL || templates[objectEventIdx].objUnion.normal.trainerType == TRAINER_TYPE_BURIED)
        {
            sVsSeeker->trainerInfo[vsSeekerObjectIdx].script = templates[objectEventIdx].script;
            sVsSeeker->trainerInfo[vsSeekerObjectIdx].trainerIdx = GetTrainerFlagFromScript(templates[objectEventIdx].script);
            sVsSeeker->trainerInfo[vsSeekerObjectIdx].localId = templates[objectEventIdx].localId;
            TryGetObjectEventIdByLocalIdAndMap(templates[objectEventIdx].localId, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup, &objectEventId);
            sVsSeeker->trainerInfo[vsSeekerObjectIdx].objectEventId = objectEventId;
            sVsSeeker->trainerInfo[vsSeekerObjectIdx].xCoord = gObjectEvents[objectEventId].currentCoords.x - 7;
            sVsSeeker->trainerInfo[vsSeekerObjectIdx].yCoord = gObjectEvents[objectEventId].currentCoords.y - 7;
            sVsSeeker->trainerInfo[vsSeekerObjectIdx].graphicsId = templates[objectEventIdx].graphicsId;
            vsSeekerObjectIdx++;
        }
    }
    sVsSeeker->trainerInfo[vsSeekerObjectIdx].localId = 0xFF;
}

static void Task_VsSeeker_3(u8 taskId)
{
    if (ScriptMovement_IsObjectMovementFinished(0xFF, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup))
    {
        if (sVsSeeker->responseCode == VSSEEKER_RESPONSE_NO_RESPONSE)
        {
            DisplayItemMessageOnField(taskId, 2, VSSeeker_Text_TrainersNotReady, Task_ItemUse_CloseMessageBoxAndReturnToField_VsSeeker);
        }
        else
        {
            if (sVsSeeker->responseCode == VSSEEKER_RESPONSE_FOUND_REMATCHES)
                StartAllRespondantIdleMovements();
            ClearDialogWindowAndFrame(0, TRUE);
            ClearPlayerHeldMovementAndUnfreezeObjectEvents();
            ScriptContext2_Disable();
            DestroyTask(taskId);
        }
        Free(sVsSeeker);
    }
}

static u8 CanUseVsSeeker(void)
{
    u8 vsSeekerChargeSteps = gSaveBlock1Ptr->trainerRematchStepCounter;
    if (vsSeekerChargeSteps == 100)
    {
        if (GetRematchableTrainerLocalId() == 0xFF)
            return VSSEEKER_NO_ONE_IN_RANGE;
        else
            return VSSEEKER_CAN_USE;
    }
    else
    {
        TV_PrintIntToStringVar(0, 100 - vsSeekerChargeSteps);
        return VSSEEKER_NOT_CHARGED;
    }
}

static u8 GetVsSeekerResponseInArea(const VsSeekerData * vsSeekerData)
{
    u16 trainerIdx = 0;
    u16 rval = 0;
    u8 rematchTrainerIdx;
    u8 unusedIdx = 0;
    u8 response = 0;
    s32 vsSeekerIdx = 0;

    while (sVsSeeker->trainerInfo[vsSeekerIdx].localId != 0xFF)
    {
        if (IsTrainerVisibleOnScreen(&sVsSeeker->trainerInfo[vsSeekerIdx]) == TRUE)
        {
            trainerIdx = sVsSeeker->trainerInfo[vsSeekerIdx].trainerIdx;
            if (!HasTrainerBeenFought(trainerIdx))
            {
                StartTrainerObjectMovementScript(&sVsSeeker->trainerInfo[vsSeekerIdx], sMovementScript_TrainerUnfought);
                sVsSeeker->trainerHasNotYetBeenFought = 1;
                vsSeekerIdx++;
                continue;
            }
            rematchTrainerIdx = GetNextAvailableRematchTrainer(vsSeekerData, trainerIdx, &unusedIdx);
            if (rematchTrainerIdx == 0)
            {
                StartTrainerObjectMovementScript(&sVsSeeker->trainerInfo[vsSeekerIdx], sMovementScript_TrainerNoRematch);
                sVsSeeker->trainerDoesNotWantRematch = 1;
            }
            else
            {
                rval = Random() % 100; // Even if it's overwritten below, it progresses the RNG.
                response = GetCurVsSeekerResponse(vsSeekerIdx, trainerIdx);
                if (response == VSSEEKER_SINGLE_RESP_YES)
                    rval = 100; // Definitely yes
                else if (response == VSSEEKER_SINGLE_RESP_NO)
                    rval = 0; // Definitely no
                // Otherwise it's a 70% chance to want a rematch
                if (rval < 30)
                {
                    StartTrainerObjectMovementScript(&sVsSeeker->trainerInfo[vsSeekerIdx], sMovementScript_TrainerNoRematch);
                    sVsSeeker->trainerDoesNotWantRematch = 1;
                }
                else
                {
                    gSaveBlock1Ptr->trainerRematches[sVsSeeker->trainerInfo[vsSeekerIdx].localId] = rematchTrainerIdx;
                    ShiftStillObjectEventCoords(&gObjectEvents[sVsSeeker->trainerInfo[vsSeekerIdx].objectEventId]);
                    StartTrainerObjectMovementScript(&sVsSeeker->trainerInfo[vsSeekerIdx], sMovementScript_TrainerRematch);
                    sVsSeeker->trainerIdxArray[sVsSeeker->numRematchableTrainers] = trainerIdx;
                    sVsSeeker->runningBehaviourEtcArray[sVsSeeker->numRematchableTrainers] = GetRunningBehaviorFromGraphicsId(sVsSeeker->trainerInfo[vsSeekerIdx].graphicsId);
                    sVsSeeker->numRematchableTrainers++;
                    sVsSeeker->trainerWantsRematch = 1;
                }
            }
        }
        vsSeekerIdx++;
    }

    if (sVsSeeker->trainerWantsRematch)
    {
        PlaySE(SE_PIN);
        FlagSet(FLAG_SYS_VS_SEEKER_CHARGING);
        VsSeekerResetChargingStepCounter();
        return VSSEEKER_RESPONSE_FOUND_REMATCHES;
    }
    if (sVsSeeker->trainerHasNotYetBeenFought)
        return VSSEEKER_RESPONSE_UNFOUGHT_TRAINERS;
    return VSSEEKER_RESPONSE_NO_RESPONSE;
}

void ClearRematchStateByTrainerId(void)
{
    u8 objEventId = 0;
    struct ObjectEventTemplate *objectEventTemplates = gSaveBlock1Ptr->objectEventTemplates;
    int vsSeekerDataIdx = LookupVsSeekerOpponentInArray(sVsSeekerData, gTrainerBattleOpponent_A);

    if (vsSeekerDataIdx != -1)
    {
        int i;

        for (i = 0; i < gMapHeader.events->objectEventCount; i++)
        {
            if ((objectEventTemplates[i].objUnion.normal.trainerType == TRAINER_TYPE_NORMAL 
              || objectEventTemplates[i].objUnion.normal.trainerType == TRAINER_TYPE_BURIED)
              && vsSeekerDataIdx == LookupVsSeekerOpponentInArray(sVsSeekerData, GetTrainerFlagFromScript(objectEventTemplates[i].script)))
            {
                struct ObjectEvent *objectEvent;

                TryGetObjectEventIdByLocalIdAndMap(objectEventTemplates[i].localId, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup, &objEventId);
                objectEvent = &gObjectEvents[objEventId];
                GetRandomFaceDirectionMovementType(&objectEventTemplates[i]); // You are using this function incorrectly.  Please consult the manual.
                OverrideMovementTypeForObjectEvent(objectEvent, sFaceDirectionMovementTypeByFacingDirection[objectEvent->facingDirection]);
                gSaveBlock1Ptr->trainerRematches[objectEventTemplates[i].localId] = 0;
                if (gSelectedObjectEvent == objEventId)
                    objectEvent->movementType = sFaceDirectionMovementTypeByFacingDirection[objectEvent->facingDirection];
                else
                    objectEvent->movementType = MOVEMENT_TYPE_FACE_DOWN;
            }
        }
    }
}

static void TryGetRematchTrainerIdGivenGameState(const u16 * trainerIdxs, u8 * rematchIdx_p)
{
    switch (*rematchIdx_p)
    {
        case 0:
            break;
        case 1:
            if (!FlagGet(FLAG_GOT_VS_SEEKER))
                *rematchIdx_p = GetRematchTrainerIdGivenGameState(trainerIdxs, *rematchIdx_p);
            break;
        case 2:
            if (!FlagGet(FLAG_WORLD_MAP_CELADON_CITY))
                *rematchIdx_p = GetRematchTrainerIdGivenGameState(trainerIdxs, *rematchIdx_p);
            break;
        case 3:
            if (!FlagGet(FLAG_WORLD_MAP_FUCHSIA_CITY))
                *rematchIdx_p = GetRematchTrainerIdGivenGameState(trainerIdxs, *rematchIdx_p);
            break;
        case 4:
            if (!FlagGet(FLAG_SYS_GAME_CLEAR))
                *rematchIdx_p = GetRematchTrainerIdGivenGameState(trainerIdxs, *rematchIdx_p);
            break;
        case 5:
            if (!FlagGet(FLAG_SYS_CAN_LINK_WITH_RS))
                *rematchIdx_p = GetRematchTrainerIdGivenGameState(trainerIdxs, *rematchIdx_p);
            break;
    }
}

static u8 GetRematchTrainerIdGivenGameState(const u16 *trainerIdxs, u8 rematchIdx)
{
    while (--rematchIdx != 0)
    {
        const u16 *rematch_p = trainerIdxs + rematchIdx;
        if (*rematch_p != 0xFFFF)
            return rematchIdx;
    }
    return 0;
}

bool8 ShouldTryRematchBattle(void)
{
    if (ShouldTryRematchBattleInternal(sVsSeekerData, gTrainerBattleOpponent_A))
    {
        return TRUE;
    }
    return HasRematchTrainerAlreadyBeenFought(sVsSeekerData, gTrainerBattleOpponent_A);
}

static bool8 ShouldTryRematchBattleInternal(const VsSeekerData *vsSeekerData, u16 trainerBattleOpponent)
{
    s32 rematchIdx = GetRematchIdx(vsSeekerData, trainerBattleOpponent);

    if (rematchIdx == -1)
        return FALSE;
    if (rematchIdx >= 0 && rematchIdx < NELEMS(sVsSeekerData))
    {
        if (IsThisTrainerRematchable(gSpecialVar_LastTalked))
            return TRUE;
    }
    return FALSE;
}

static bool8 HasRematchTrainerAlreadyBeenFought(const VsSeekerData *vsSeekerData, u16 trainerBattleOpponent)
{
    s32 rematchIdx = GetRematchIdx(vsSeekerData, trainerBattleOpponent);

    if (rematchIdx == -1)
        return FALSE;
    if (!HasTrainerBeenFought(vsSeekerData[rematchIdx].trainerIdxs[0]))
        return FALSE;
    return TRUE;
}

void ClearRematchStateOfLastTalked(void)
{
    gSaveBlock1Ptr->trainerRematches[gSpecialVar_LastTalked] = 0;
    SetBattledTrainerFlag();
}

static int LookupVsSeekerOpponentInArray(const VsSeekerData * array, u16 trainerId)
{
    int i, j;

    for (i = 0; i < NELEMS(sVsSeekerData); i++)
    {
        for (j = 0; j < 6; j++)
        {
            u16 testTrainerId;
            if (array[i].trainerIdxs[j] == 0)
                break;
            testTrainerId = array[i].trainerIdxs[j];
            if (testTrainerId == 0xFFFF)
                continue;
            if (testTrainerId == trainerId)
                return i;
        }
    }

    return -1;
}

int GetRematchTrainerId(u16 trainerId)
{
    u8 i;
    u8 j;
    j = GetNextAvailableRematchTrainer(sVsSeekerData, trainerId, &i);
    if (!j)
        return 0;
    TryGetRematchTrainerIdGivenGameState(sVsSeekerData[i].trainerIdxs, &j);
    return sVsSeekerData[i].trainerIdxs[j];
}

u8 IsTrainerReadyForRematch(void)
{
    return IsTrainerReadyForRematchInternal(sVsSeekerData, gTrainerBattleOpponent_A);
}

static bool8 IsTrainerReadyForRematchInternal(const VsSeekerData * array, u16 trainerId)
{
    int rematchTrainerIdx = LookupVsSeekerOpponentInArray(array, trainerId);

    if (rematchTrainerIdx == -1)
        return FALSE;
    if (rematchTrainerIdx >= NELEMS(sVsSeekerData))
        return FALSE;
    if (!IsThisTrainerRematchable(gSpecialVar_LastTalked))
        return FALSE;
    return TRUE;
}

static bool8 ObjectEventIdIsSane(u8 objectEventId)
{
    struct ObjectEvent *objectEvent = &gObjectEvents[objectEventId];

    if (objectEvent->active && gMapHeader.events->objectEventCount >= objectEvent->localId && gSprites[objectEvent->spriteId].data[0] == objectEventId)
        return TRUE;
    return FALSE;
}

static u8 GetRandomFaceDirectionMovementType()
{
    u16 r1 = Random() % 4;

    switch (r1)
    {
        case 0:
            return MOVEMENT_TYPE_FACE_UP;
        case 1:
            return MOVEMENT_TYPE_FACE_DOWN;
        case 2:
            return MOVEMENT_TYPE_FACE_LEFT;
        case 3:
            return MOVEMENT_TYPE_FACE_RIGHT;
        default:
            return MOVEMENT_TYPE_FACE_DOWN;
    }
}

static u8 GetRunningBehaviorFromGraphicsId(u8 graphicsId)
{
    switch (graphicsId)
    {
        case OBJ_EVENT_GFX_LITTLE_GIRL:
        case OBJ_EVENT_GFX_YOUNGSTER:
        case OBJ_EVENT_GFX_BOY:
        case OBJ_EVENT_GFX_BUG_CATCHER:
        case OBJ_EVENT_GFX_LASS:
        case OBJ_EVENT_GFX_WOMAN_1:
        case OBJ_EVENT_GFX_BATTLE_GIRL:
        case OBJ_EVENT_GFX_MAN:
        case OBJ_EVENT_GFX_ROCKER:
        case OBJ_EVENT_GFX_WOMAN_2:
        case OBJ_EVENT_GFX_BEAUTY:
        case OBJ_EVENT_GFX_BALDING_MAN:
        case OBJ_EVENT_GFX_TUBER_F:
        case OBJ_EVENT_GFX_CAMPER:
        case OBJ_EVENT_GFX_PICNICKER:
        case OBJ_EVENT_GFX_COOLTRAINER_M:
        case OBJ_EVENT_GFX_COOLTRAINER_F:
        case OBJ_EVENT_GFX_SWIMMER_M_LAND:
        case OBJ_EVENT_GFX_SWIMMER_F_LAND:
        case OBJ_EVENT_GFX_BLACKBELT:
        case OBJ_EVENT_GFX_HIKER:
        case OBJ_EVENT_GFX_SAILOR:
            return MOVEMENT_TYPE_VS_SEEKER_4E;
        case OBJ_EVENT_GFX_TUBER_M_WATER:
        case OBJ_EVENT_GFX_SWIMMER_M_WATER:
        case OBJ_EVENT_GFX_SWIMMER_F_WATER:
            return MOVEMENT_TYPE_VS_SEEKER_4F;
        default:
            return MOVEMENT_TYPE_VS_SEEKER_4D;
    }
}

static u16 GetTrainerFlagFromScript(const u8 *script)
/*
 * The trainer flag is a little-endian short located +2 from
 * the script pointer, assuming the trainerbattle command is
 * first in the script.  Because scripts are unaligned, and
 * because the ARM processor requires shorts to be 16-bit
 * aligned, this function needs to perform explicit bitwise
 * operations to get the correct flag.
 *
 * 5c XX YY ZZ ...
 *       -- --
 */
{
    u16 trainerFlag;

    script += 2;
    trainerFlag = script[0];
    trainerFlag |= script[1] << 8;
    return trainerFlag;
}

static int GetRematchIdx(const VsSeekerData * vsSeekerData, u16 trainerFlagIdx)
{
    int i;

    for (i = 0; i < NELEMS(sVsSeekerData); i++)
    {
        if (vsSeekerData[i].trainerIdxs[0] == trainerFlagIdx)
            return i;
    }

    return -1;
}

static bool32 IsThisTrainerRematchable(u32 localId)
{
    if (!gSaveBlock1Ptr->trainerRematches[localId])
        return FALSE;
    return TRUE;
}

static void ClearAllTrainerRematchStates(void)
{
    u8 i;

    for (i = 0; i < NELEMS(gSaveBlock1Ptr->trainerRematches); i++)
        gSaveBlock1Ptr->trainerRematches[i] = 0;
}

static bool8 IsTrainerVisibleOnScreen(struct VsSeekerTrainerInfo * trainerInfo)
{
    s16 x;
    s16 y;

    PlayerGetDestCoords(&x, &y);
    x -= 7;
    y -= 7;

    if (   x - 7 <= trainerInfo->xCoord
        && x + 7 >= trainerInfo->xCoord
        && y - 5 <= trainerInfo->yCoord
        && y + 5 >= trainerInfo->yCoord
        && ObjectEventIdIsSane(trainerInfo->objectEventId) == 1)
        return TRUE;
    return FALSE;
}

static u8 GetNextAvailableRematchTrainer(const VsSeekerData * vsSeekerData, u16 trainerFlagNo, u8 * idxPtr)
{
    int i, j;

    for (i = 0; i < NELEMS(sVsSeekerData); i++)
    {
        if (vsSeekerData[i].trainerIdxs[0] == trainerFlagNo)
        {
            *idxPtr = i;
            for (j = 1; j < 6; j++)
            {
                if (vsSeekerData[i].trainerIdxs[j] == 0)
                    return j - 1;
                if (vsSeekerData[i].trainerIdxs[j] == 0xffff)
                    continue;
                if (HasTrainerBeenFought(vsSeekerData[i].trainerIdxs[j]))
                    continue;
                return j;
            }
            return j - 1;
        }
    }

    *idxPtr = 0;
    return 0;
}

static u8 GetRematchableTrainerLocalId(void)
{
    u8 idx;
    u8 i;

    for (i = 0; sVsSeeker->trainerInfo[i].localId != 0xFF; i++)
    {
        if (IsTrainerVisibleOnScreen(&sVsSeeker->trainerInfo[i]) == 1)
        {
            if (HasTrainerBeenFought(sVsSeeker->trainerInfo[i].trainerIdx) != 1 || GetNextAvailableRematchTrainer(sVsSeekerData, sVsSeeker->trainerInfo[i].trainerIdx, &idx))
                return sVsSeeker->trainerInfo[i].localId;
        }
    }

    return 0xFF;
}

static void StartTrainerObjectMovementScript(struct VsSeekerTrainerInfo * trainerInfo, const u8 * script)
{
    UnfreezeObjectEvent(&gObjectEvents[trainerInfo->objectEventId]);
    ScriptMovement_StartObjectMovementScript(trainerInfo->localId, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup, script);
}

static u8 GetCurVsSeekerResponse(s32 vsSeekerIdx, u16 trainerIdx)
{
    s32 i;
    s32 j;

    for (i = 0; i < vsSeekerIdx; i++)
    {
        if (IsTrainerVisibleOnScreen(&sVsSeeker->trainerInfo[i]) == 1 && sVsSeeker->trainerInfo[i].trainerIdx == trainerIdx)
        {
            for (j = 0; j < sVsSeeker->numRematchableTrainers; j++)
            {
                if (sVsSeeker->trainerIdxArray[j] == sVsSeeker->trainerInfo[i].trainerIdx)
                    return VSSEEKER_SINGLE_RESP_YES;
            }
            return VSSEEKER_SINGLE_RESP_NO;
        }
    }
    return VSSEEKER_SINGLE_RESP_RAND;
}

static void StartAllRespondantIdleMovements(void)
{
    u8 dummy = 0;
    s32 i;
    s32 j;

    for (i = 0; i < sVsSeeker->numRematchableTrainers; i++)
    {
        for (j = 0; sVsSeeker->trainerInfo[j].localId != 0xFF; j++)
        {
            if (sVsSeeker->trainerInfo[j].trainerIdx == sVsSeeker->trainerIdxArray[i])
            {
                struct ObjectEvent *objectEvent = &gObjectEvents[sVsSeeker->trainerInfo[j].objectEventId];

                if (ObjectEventIdIsSane(sVsSeeker->trainerInfo[j].objectEventId) == 1)
                    SetTrainerMovementType(objectEvent, sVsSeeker->runningBehaviourEtcArray[i]);
                OverrideMovementTypeForObjectEvent(objectEvent, sVsSeeker->runningBehaviourEtcArray[i]);
                gSaveBlock1Ptr->trainerRematches[sVsSeeker->trainerInfo[j].localId] = GetNextAvailableRematchTrainer(sVsSeekerData, sVsSeeker->trainerInfo[j].trainerIdx, &dummy);
            }
        }
    }
}
