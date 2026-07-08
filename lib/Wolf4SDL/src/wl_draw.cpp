// WL_DRAW.C

#include "wl_def.h"

#include "wl_cloudsky.h"
#include "wl_atmos.h"
#include "wl_shade.h"

#ifdef WOLF3D_CYD_PORT
#include <esp_heap_caps.h>
#include "../../../include/board_config.h"
extern "C" void furi_log_print_format(int, const char *, const char *, ...);
extern "C" void cyd_poll_touch_controls(void);
extern "C" void cyd_sound_poll(void);
extern "C" uint32_t cyd_perf_micros(void);
extern "C" void cyd_perf_record_sprites(uint16_t visible, uint16_t drawn, uint16_t decor,
                                        uint16_t bonus, uint16_t actor,
                                        uint32_t decorUs, uint32_t bonusUs, uint32_t actorUs);
extern "C" void cyd_perf_record_walltex(uint32_t lookups, uint32_t hits, uint32_t builds,
                                        uint32_t buildUs);
extern "C" void cyd_perf_record_render_phases(uint32_t prepUs, uint32_t clearUs,
                                              uint32_t wallUs, uint32_t spriteUs,
                                              uint32_t weaponUs, uint32_t presentUs);
extern "C" void cyd_trace_frame(void);
extern "C" void cyd_trace_sprite(int shapenum, int category, unsigned height);
#ifndef CYD_WOLF_DRAW_SPRITES
#define CYD_WOLF_DRAW_SPRITES 1
#endif
#ifndef CYD_WOLF_DRAW_WEAPON
#define CYD_WOLF_DRAW_WEAPON 0
#endif
#ifndef CYD_WOLF_DRAW_STATUSBAR_ART
#define CYD_WOLF_DRAW_STATUSBAR_ART 0
#endif
#ifndef CYD_WOLF_FLAT_WALLS
#define CYD_WOLF_FLAT_WALLS 1
#endif
#ifndef CYD_WOLF_LOW_RES_WALL_TEXTURES
#define CYD_WOLF_LOW_RES_WALL_TEXTURES 0
#endif
#ifndef CYD_WOLF_WALL_TEXTURE_CACHE
#define CYD_WOLF_WALL_TEXTURE_CACHE 1
#endif
#ifndef CYD_WOLF_WALL_TEXTURE_CACHE_SLOTS
#define CYD_WOLF_WALL_TEXTURE_CACHE_SLOTS 8
#endif
#ifndef CYD_WOLF_WALL_TEXTURE_DIM
#define CYD_WOLF_WALL_TEXTURE_DIM 16
#endif
#ifndef CYD_WOLF_WALL_COLUMN_STEP
#define CYD_WOLF_WALL_COLUMN_STEP 1
#endif
#if CYD_WOLF_WALL_COLUMN_STEP < 1
#undef CYD_WOLF_WALL_COLUMN_STEP
#define CYD_WOLF_WALL_COLUMN_STEP 1
#endif
#ifndef CYD_WOLF_WALL_TEXTURE_MIN_PIC
#define CYD_WOLF_WALL_TEXTURE_MIN_PIC 0
#endif
#ifndef CYD_WOLF_FAST_SPRITES
#define CYD_WOLF_FAST_SPRITES 1
#endif
#ifndef CYD_WOLF_FAST_SPRITE_MIN_HEIGHT
#define CYD_WOLF_FAST_SPRITE_MIN_HEIGHT 96
#endif
#ifndef CYD_WOLF_FAST_DECOR_SPRITE_MIN_HEIGHT
#define CYD_WOLF_FAST_DECOR_SPRITE_MIN_HEIGHT 32
#endif
#ifndef CYD_WOLF_FAST_WEAPON_SPRITES
#define CYD_WOLF_FAST_WEAPON_SPRITES 1
#endif
#ifndef CYD_WOLF_DOWNSAMPLED_WEAPON
#define CYD_WOLF_DOWNSAMPLED_WEAPON 0
#endif
#ifndef CYD_WOLF_WEAPON_CACHE_DIM
#define CYD_WOLF_WEAPON_CACHE_DIM 32
#endif
#ifndef CYD_WOLF_WEAPON_CACHE_SLOTS
#define CYD_WOLF_WEAPON_CACHE_SLOTS 2
#endif
#ifndef CYD_WOLF_ACTOR_IMPOSTORS
#define CYD_WOLF_ACTOR_IMPOSTORS 0
#endif
#ifndef CYD_WOLF_HIDE_TINY_DECOR_SPRITES
#define CYD_WOLF_HIDE_TINY_DECOR_SPRITES 1
#endif
#ifndef CYD_WOLF_TINY_DECOR_MAX_HEIGHT
#define CYD_WOLF_TINY_DECOR_MAX_HEIGHT 14
#endif
#ifndef CYD_WOLF_DRAW_STATIC_DECOR
#define CYD_WOLF_DRAW_STATIC_DECOR 1
#endif
#ifndef CYD_WOLF_MAX_STATIC_DECOR_SPRITES
#define CYD_WOLF_MAX_STATIC_DECOR_SPRITES 8
#endif
#ifndef CYD_WOLF_STATIC_DECOR_IMPOSTORS
#define CYD_WOLF_STATIC_DECOR_IMPOSTORS 1
#endif
#ifndef CYD_WOLF_STATIC_DECOR_CACHE
#define CYD_WOLF_STATIC_DECOR_CACHE 1
#endif
#ifndef CYD_WOLF_DECOR_CACHE_COUNT
#define CYD_WOLF_DECOR_CACHE_COUNT 16
#endif
#ifndef CYD_WOLF_DECOR_OCCLUSION_MARGIN
#define CYD_WOLF_DECOR_OCCLUSION_MARGIN 16
#endif
#ifndef CYD_WOLF_SPRITE_BUDGET_US
#define CYD_WOLF_SPRITE_BUDGET_US 0
#endif
#ifndef CYD_WOLF_ENABLE_FRAME_HEARTBEAT
#define CYD_WOLF_ENABLE_FRAME_HEARTBEAT 0
#endif
#ifndef CYD_WOLF_ENABLE_PERF_LOGS
#define CYD_WOLF_ENABLE_PERF_LOGS 0
#endif
#ifndef CYD_WOLF_SCREEN_FLASHES
#define CYD_WOLF_SCREEN_FLASHES 1
#endif

#define CYD_VIS_BONUS        0x1000
#define CYD_VIS_ACTOR        0x2000
#define CYD_VIS_STATIC_DECOR 0x4000

#define CYD_TRACE_SPRITE_DECOR 1
#define CYD_TRACE_SPRITE_BONUS 2
#define CYD_TRACE_SPRITE_ACTOR 3
#define CYD_TRACE_SPRITE_WEAPON 4

static bool CydAlwaysKeepDecorSprite(int shapenum)
{
    // Barrels are blocking gameplay objects and visually useful for navigation.
    // The generic decor cap can otherwise drop them in busy rooms.
    return shapenum == SPR_STAT_1 || shapenum == SPR_STAT_35;
}
#endif

/*
=============================================================================

                               LOCAL CONSTANTS

=============================================================================
*/

// the door is the last picture before the sprites
#define DOORWALL        (PMSpriteStart-8)

#define ACTORSIZE       0x4000

/*
=============================================================================

                              GLOBAL VARIABLES

=============================================================================
*/

static byte *vbuf = NULL;
unsigned vbufPitch = 0;

int32_t    lasttimecount;
int32_t    frameon;
boolean fpscounter;

int fps_frames=0, fps_time=0, fps=0;

int *wallheight;
int min_wallheight;

//
// math tables
//
short *pixelangle;
// Precomputed trigonometric tables
const int32_t finetangent[900] = {
    57,    171,    285,    400,    514,    629,    743,    857,
    972,    1086,    1201,    1315,    1430,    1544,    1658,    1773,
    1887,    2002,    2116,    2231,    2345,    2460,    2574,    2689,
    2804,    2918,    3033,    3147,    3262,    3377,    3491,    3606,
    3721,    3836,    3950,    4065,    4180,    4295,    4410,    4525,
    4640,    4755,    4870,    4985,    5100,    5215,    5330,    5445,
    5560,    5676,    5791,    5906,    6021,    6137,    6252,    6368,
    6483,    6599,    6714,    6830,    6945,    7061,    7177,    7293,
    7408,    7524,    7640,    7756,    7872,    7988,    8104,    8221,
    8337,    8453,    8569,    8686,    8802,    8919,    9035,    9152,
    9268,    9385,    9502,    9619,    9735,    9852,    9969,    10086,
    10204,    10321,    10438,    10555,    10673,    10790,    10908,    11025,
    11143,    11261,    11378,    11496,    11614,    11732,    11850,    11968,
    12087,    12205,    12323,    12442,    12560,    12679,    12798,    12917,
    13035,    13154,    13273,    13393,    13512,    13631,    13750,    13870,
    13989,    14109,    14229,    14349,    14468,    14588,    14709,    14829,
    14949,    15069,    15190,    15311,    15431,    15552,    15673,    15794,
    15915,    16036,    16157,    16279,    16400,    16522,    16644,    16765,
    16887,    17009,    17131,    17254,    17376,    17499,    17621,    17744,
    17867,    17990,    18113,    18236,    18359,    18483,    18606,    18730,
    18854,    18977,    19102,    19226,    19350,    19474,    19599,    19724,
    19848,    19973,    20098,    20224,    20349,    20474,    20600,    20726,
    20852,    20978,    21104,    21230,    21357,    21483,    21610,    21737,
    21864,    21991,    22118,    22246,    22374,    22501,    22629,    22757,
    22886,    23014,    23143,    23271,    23400,    23529,    23659,    23788,
    23917,    24047,    24177,    24307,    24437,    24568,    24698,    24829,
    24960,    25091,    25222,    25353,    25485,    25617,    25749,    25881,
    26013,    26146,    26278,    26411,    26544,    26678,    26811,    26945,
    27078,    27212,    27347,    27481,    27616,    27750,    27885,    28021,
    28156,    28292,    28427,    28563,    28700,    28836,    28973,    29110,
    29247,    29384,    29521,    29659,    29797,    29935,    30073,    30212,
    30351,    30490,    30629,    30769,    30908,    31048,    31188,    31329,
    31469,    31610,    31751,    31893,    32034,    32176,    32318,    32461,
    32603,    32746,    32889,    33032,    33176,    33320,    33464,    33608,
    33753,    33898,    34043,    34188,    34334,    34480,    34626,    34772,
    34919,    35066,    35213,    35361,    35509,    35657,    35805,    35954,
    36103,    36252,    36401,    36551,    36701,    36852,    37003,    37154,
    37305,    37456,    37608,    37761,    37913,    38066,    38219,    38372,
    38526,    38680,    38835,    38989,    39144,    39300,    39455,    39611,
    39768,    39924,    40081,    40239,    40396,    40554,    40713,    40871,
    41031,    41190,    41350,    41510,    41670,    41831,    41992,    42154,
    42316,    42478,    42640,    42803,    42967,    43131,    43295,    43459,
    43624,    43789,    43955,    44121,    44287,    44454,    44621,    44789,
    44957,    45125,    45294,    45463,    45633,    45803,    45974,    46144,
    46316,    46487,    46660,    46832,    47005,    47179,    47353,    47527,
    47702,    47877,    48052,    48229,    48405,    48582,    48760,    48938,
    49116,    49295,    49474,    49654,    49834,    50015,    50196,    50378,
    50560,    50743,    50926,    51110,    51294,    51479,    51664,    51850,
    52036,    52223,    52410,    52598,    52786,    52975,    53164,    53354,
    53545,    53736,    53927,    54119,    54312,    54505,    54699,    54893,
    55088,    55284,    55480,    55676,    55874,    56072,    56270,    56469,
    56669,    56869,    57070,    57271,    57473,    57676,    57879,    58083,
    58287,    58493,    58698,    58905,    59112,    59320,    59528,    59737,
    59947,    60157,    60369,    60580,    60793,    61006,    61220,    61434,
    61650,    61866,    62082,    62300,    62518,    62737,    62956,    63176,
    63397,    63619,    63842,    64065,    64289,    64514,    64740,    64966,
    65193,    65421,    65650,    65880,    66110,    66341,    66573,    66806,
    67040,    67274,    67509,    67746,    67983,    68221,    68459,    68699,
    68939,    69181,    69423,    69666,    69910,    70155,    70401,    70648,
    70896,    71145,    71394,    71645,    71896,    72149,    72403,    72657,
    72912,    73169,    73426,    73685,    73944,    74205,    74466,    74729,
    74993,    75257,    75523,    75790,    76058,    76327,    76597,    76868,
    77140,    77414,    77688,    77964,    78241,    78519,    78798,    79078,
    79360,    79643,    79926,    80212,    80498,    80785,    81074,    81364,
    81656,    81948,    82242,    82537,    82834,    83132,    83431,    83731,
    84033,    84336,    84640,    84946,    85254,    85562,    85872,    86184,
    86497,    86811,    87127,    87444,    87763,    88083,    88405,    88728,
    89053,    89379,    89707,    90037,    90368,    90700,    91035,    91371,
    91708,    92047,    92388,    92731,    93075,    93421,    93769,    94118,
    94469,    94822,    95177,    95533,    95892,    96252,    96614,    96978,
    97344,    97711,    98081,    98453,    98826,    99202,    99579,    99959,
    100340,    100724,    101109,    101497,    101887,    102279,    102673,    103069,
    103467,    103868,    104271,    104676,    105083,    105493,    105904,    106319,
    106735,    107154,    107576,    107999,    108426,    108854,    109286,    109719,
    110156,    110594,    111036,    111480,    111927,    112376,    112828,    113283,
    113740,    114201,    114664,    115130,    115598,    116070,    116545,    117022,
    117503,    117987,    118473,    118963,    119456,    119952,    120451,    120953,
    121459,    121968,    122480,    122996,    123515,    124037,    124563,    125092,
    125625,    126162,    126702,    127246,    127793,    128344,    128899,    129458,
    130021,    130587,    131158,    131732,    132311,    132893,    133480,    134071,
    134666,    135266,    135870,    136478,    137091,    137708,    138329,    138956,
    139587,    140222,    140863,    141508,    142158,    142813,    143473,    144138,
    144809,    145484,    146165,    146851,    147542,    148239,    148941,    149649,
    150363,    151082,    151808,    152539,    153276,    154019,    154768,    155523,
    156285,    157053,    157828,    158609,    159396,    160191,    160992,    161800,
    162615,    163437,    164267,    165103,    165947,    166799,    167658,    168525,
    169400,    170282,    171173,    172072,    172979,    173894,    174818,    175751,
    176692,    177643,    178602,    179570,    180548,    181536,    182532,    183539,
    184555,    185582,    186619,    187666,    188723,    189792,    190871,    191961,
    193062,    194175,    195299,    196435,    197584,    198744,    199916,    201101,
    202299,    203510,    204734,    205971,    207222,    208487,    209766,    211060,
    212368,    213691,    215029,    216383,    217752,    219137,    220538,    221956,
    223391,    224843,    226313,    227800,    229306,    230830,    232373,    233935,
    235516,    237118,    238740,    240383,    242047,    243732,    245440,    247170,
    248923,    250699,    252499,    254324,    256173,    258048,    259949,    261876,
    263831,    265813,    267823,    269862,    271931,    274030,    276160,    278321,
    280515,    282741,    285002,    287297,    289627,    291994,    294397,    296839,
    299320,    301840,    304401,    307005,    309651,    312341,    315076,    317857,
    320687,    323564,    326492,    329471,    332503,    335589,    338731,    341930,
    345188,    348506,    351886,    355330,    358840,    362418,    366066,    369785,
    373579,    377448,    381397,    385426,    389539,    393738,    398026,    402406,
    406880,    411453,    416127,    420906,    425794,    430793,    435909,    441144,
    446504,    451993,    457616,    463378,    469284,    475339,    481549,    487921,
    494460,    501174,    508070,    515155,    522436,    529924,    537626,    545551,
    553710,    562114,    570773,    579699,    588905,    598404,    608212,    618342,
    628811,    639637,    650838,    662434,    674447,    686899,    699816,    713223,
    727149,    741625,    756684,    772363,    788701,    805739,    823525,    842108,
    861544,    881893,    903221,    925600,    949110,    973839,    999886,    1027357,
    1056374,    1087071,    1119598,    1154125,    1190841,    1229963,    1271734,    1316434,
    1364382,    1415946,    1471551,    1531692,    1596949,    1668002,    1745662,    1830894,
    1924864,    2028989,    2145010,    2275089,    2421948,    2589058,    2780919,    3003472,
    3264723,    3575729,    3952202,    4417247,    5006295,    5776577,    6826947,    8344131,
    10728255,    15019649,    25032850,    75098705,
};

const fixed sintable[450] = {
    0,    1143,    2287,    3429,    4571,    5711,    6850,    7986,
    9120,    10252,    11380,    12504,    13625,    14742,    15854,    16961,
    18064,    19160,    20251,    21336,    22414,    23486,    24550,    25606,
    26655,    27696,    28729,    29752,    30767,    31772,    32768,    33753,
    34728,    35693,    36647,    37589,    38521,    39440,    40347,    41243,
    42125,    42995,    43852,    44695,    45525,    46340,    47142,    47929,
    48702,    49460,    50203,    50931,    51643,    52339,    53019,    53683,
    54331,    54963,    55577,    56175,    56755,    57319,    57864,    58393,
    58903,    59395,    59870,    60326,    60763,    61183,    61583,    61965,
    62328,    62672,    62997,    63302,    63589,    63856,    64103,    64331,
    64540,    64729,    64898,    65047,    65176,    65286,    65376,    65446,
    65496,    65526,    65536,    65526,    65496,    65446,    65376,    65286,
    65176,    65047,    64898,    64729,    64540,    64331,    64103,    63856,
    63589,    63302,    62997,    62672,    62328,    61965,    61583,    61183,
    60763,    60326,    59870,    59395,    58903,    58393,    57864,    57319,
    56755,    56175,    55577,    54963,    54331,    53683,    53019,    52339,
    51643,    50931,    50203,    49460,    48702,    47929,    47142,    46340,
    45525,    44695,    43852,    42995,    42125,    41243,    40347,    39440,
    38521,    37589,    36647,    35693,    34728,    33753,    32768,    31772,
    30767,    29752,    28729,    27696,    26655,    25606,    24550,    23486,
    22414,    21336,    20251,    19160,    18064,    16961,    15854,    14742,
    13625,    12504,    11380,    10252,    9120,    7986,    6850,    5711,
    4571,    3429,    2287,    1143,    0,    -1143,    -2287,    -3429,
    -4571,    -5711,    -6850,    -7986,    -9120,    -10252,    -11380,    -12504,
    -13625,    -14742,    -15854,    -16961,    -18064,    -19160,    -20251,    -21336,
    -22414,    -23486,    -24550,    -25606,    -26655,    -27696,    -28729,    -29752,
    -30767,    -31772,    -32768,    -33753,    -34728,    -35693,    -36647,    -37589,
    -38521,    -39440,    -40347,    -41243,    -42125,    -42995,    -43852,    -44695,
    -45525,    -46340,    -47142,    -47929,    -48702,    -49460,    -50203,    -50931,
    -51643,    -52339,    -53019,    -53683,    -54331,    -54963,    -55577,    -56175,
    -56755,    -57319,    -57864,    -58393,    -58903,    -59395,    -59870,    -60326,
    -60763,    -61183,    -61583,    -61965,    -62328,    -62672,    -62997,    -63302,
    -63589,    -63856,    -64103,    -64331,    -64540,    -64729,    -64898,    -65047,
    -65176,    -65286,    -65376,    -65446,    -65496,    -65526,    -65536,    -65526,
    -65496,    -65446,    -65376,    -65286,    -65176,    -65047,    -64898,    -64729,
    -64540,    -64331,    -64103,    -63856,    -63589,    -63302,    -62997,    -62672,
    -62328,    -61965,    -61583,    -61183,    -60763,    -60326,    -59870,    -59395,
    -58903,    -58393,    -57864,    -57319,    -56755,    -56175,    -55577,    -54963,
    -54331,    -53683,    -53019,    -52339,    -51643,    -50931,    -50203,    -49460,
    -48702,    -47929,    -47142,    -46340,    -45525,    -44695,    -43852,    -42995,
    -42125,    -41243,    -40347,    -39440,    -38521,    -37589,    -36647,    -35693,
    -34728,    -33753,    -32768,    -31772,    -30767,    -29752,    -28729,    -27696,
    -26655,    -25606,    -24550,    -23486,    -22414,    -21336,    -20251,    -19160,
    -18064,    -16961,    -15854,    -14742,    -13625,    -12504,    -11380,    -10252,
    -9120,    -7986,    -6850,    -5711,    -4571,    -3429,    -2287,    -1143,
    0,    1143,    2287,    3429,    4571,    5711,    6850,    7986,
    9120,    10252,    11380,    12504,    13625,    14742,    15854,    16961,
    18064,    19160,    20251,    21336,    22414,    23486,    24550,    25606,
    26655,    27696,    28729,    29752,    30767,    31772,    32768,    33753,
    34728,    35693,    36647,    37589,    38521,    39440,    40347,    41243,
    42125,    42995,    43852,    44695,    45525,    46340,    47142,    47929,
    48702,    49460,    50203,    50931,    51643,    52339,    53019,    53683,
    54331,    54963,    55577,    56175,    56755,    57319,    57864,    58393,
    58903,    59395,    59870,    60326,    60763,    61183,    61583,    61965,
    62328,    62672,    62997,    63302,    63589,    63856,    64103,    64331,
    64540,    64729,    64898,    65047,    65176,    65286,    65376,    65446,
    65496,    65526,
};
const fixed *costable = sintable+(ANGLES/4);

//
// refresh variables
//
fixed   viewx,viewy;                    // the focal point
short   viewangle;
fixed   viewsin,viewcos;

void    TransformActor (objtype *ob);
void    BuildTables (void);
void    ClearScreen (void);
int     CalcRotate (objtype *ob);
void    DrawScaleds (void);
void    CalcTics (void);
void    ThreeDRefresh (void);



//
// wall optimization variables
//
int     lastside;               // true for vertical
int32_t    lastintercept;
int     lasttilehit;
int     lasttexture;

//
// ray tracing variables
//
short    focaltx,focalty,viewtx,viewty;
longword xpartialup,xpartialdown,ypartialup,ypartialdown;

short   midangle,angle;

word    tilehit;
int     pixx;

short   xtile,ytile;
short   xtilestep,ytilestep;
int32_t    xintercept,yintercept;
word    xstep,ystep;
word    xspot,yspot;
int     texdelta;

word horizwall[MAXWALLTILES],vertwall[MAXWALLTILES];


/*
============================================================================

                           3 - D  DEFINITIONS

============================================================================
*/

/*
========================
=
= TransformActor
=
= Takes paramaters:
=   gx,gy               : globalx/globaly of point
=
= globals:
=   viewx,viewy         : point of view
=   viewcos,viewsin     : sin/cos of viewangle
=   scale               : conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
========================
*/


//
// transform actor
//
void TransformActor (objtype *ob)
{
    fixed gx,gy,gxt,gyt,nx,ny;

//
// translate point to view centered coordinates
//
    gx = ob->x-viewx;
    gy = ob->y-viewy;

//
// calculate newx
//
    gxt = FixedMul(gx,viewcos);
    gyt = FixedMul(gy,viewsin);
    nx = gxt-gyt-ACTORSIZE;         // fudge the shape forward a bit, because
                                    // the midpoint could put parts of the shape
                                    // into an adjacent wall

//
// calculate newy
//
    gxt = FixedMul(gx,viewsin);
    gyt = FixedMul(gy,viewcos);
    ny = gyt+gxt;

//
// calculate perspective ratio
//
    ob->transx = nx;
    ob->transy = ny;

    if (nx<MINDIST)                 // too close, don't overflow the divide
    {
        ob->viewheight = 0;
        return;
    }

    ob->viewx = (word)(centerx + ny*scale/nx);

//
// calculate height (heightnumerator/(nx>>8))
//
    ob->viewheight = (word)(heightnumerator/(nx>>8));
}

//==========================================================================

/*
========================
=
= TransformTile
=
= Takes paramaters:
=   tx,ty               : tile the object is centered in
=
= globals:
=   viewx,viewy         : point of view
=   viewcos,viewsin     : sin/cos of viewangle
=   scale               : conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
= Returns true if the tile is withing getting distance
=
========================
*/

boolean TransformTile (int tx, int ty, short *dispx, short *dispheight)
{
    fixed gx,gy,gxt,gyt,nx,ny;

//
// translate point to view centered coordinates
//
    gx = ((int32_t)tx<<TILESHIFT)+0x8000-viewx;
    gy = ((int32_t)ty<<TILESHIFT)+0x8000-viewy;

//
// calculate newx
//
    gxt = FixedMul(gx,viewcos);
    gyt = FixedMul(gy,viewsin);
    nx = gxt-gyt-0x2000;            // 0x2000 is size of object

//
// calculate newy
//
    gxt = FixedMul(gx,viewsin);
    gyt = FixedMul(gy,viewcos);
    ny = gyt+gxt;


//
// calculate height / perspective ratio
//
    if (nx<MINDIST)                 // too close, don't overflow the divide
        *dispheight = 0;
    else
    {
        *dispx = (short)(centerx + ny*scale/nx);
        *dispheight = (short)(heightnumerator/(nx>>8));
    }

//
// see if it should be grabbed
//
    if (nx<TILEGLOBAL && ny>-TILEGLOBAL/2 && ny<TILEGLOBAL/2)
        return true;
    else
        return false;
}

//==========================================================================

/*
====================
=
= CalcHeight
=
= Calculates the height of xintercept,yintercept from viewx,viewy
=
====================
*/

int CalcHeight()
{
    fixed z = FixedMul(xintercept - viewx, viewcos)
        - FixedMul(yintercept - viewy, viewsin);
    if(z < MINDIST) z = MINDIST;
    int height = heightnumerator / (z >> 8);
    if(height < min_wallheight) min_wallheight = height;
    return height;
}

//==========================================================================

/*
===================
=
= ScalePost
=
===================
*/

byte *postsource;
int postx;
int postwidth;
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_WALL_TEXTURE_CACHE
int postwallpic;

namespace {
constexpr int CYD_WALL_CACHE_DIM = CYD_WOLF_WALL_TEXTURE_DIM;
constexpr int CYD_WALL_CACHE_TARGET_SLOTS = CYD_WOLF_WALL_TEXTURE_CACHE_SLOTS;
constexpr int CYD_WALL_CACHE_MIN_SLOTS = 4;
constexpr uint32_t CYD_WALL_CACHE_HEAP_RESERVE = 1024;
struct CydWallCacheSlot {
    int wallpic = -1;
    uint32_t lastUse = 0;
    byte tex[CYD_WALL_CACHE_DIM * CYD_WALL_CACHE_DIM];
};
CydWallCacheSlot *cydWallCache = nullptr;
int cydWallCacheSlots = 0;
uint32_t cydWallCacheClock = 0;
bool cydWallCacheSkipped = false;
#if CYD_WOLF_ENABLE_PERF_LOGS
uint32_t cydWallTexLookups = 0;
uint32_t cydWallTexHits = 0;
uint32_t cydWallTexBuilds = 0;
uint32_t cydWallTexBuildUs = 0;
#endif
}

extern "C" void CydFreeWallCache(void)
{
    if (cydWallCache)
    {
        free(cydWallCache);
        cydWallCache = nullptr;
        cydWallCacheSlots = 0;
    }
    cydWallCacheSkipped = false;
}

static bool CydEnsureWallCache()
{
    if(cydWallCache)
        return true;
    if(cydWallCacheSkipped)
        return false;

    for(int slots = CYD_WALL_CACHE_TARGET_SLOTS; slots >= CYD_WALL_CACHE_MIN_SLOTS; slots -= 4)
    {
        size_t bytes = (size_t)slots * sizeof(CydWallCacheSlot);
        uint32_t freeBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        uint32_t largestBefore = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        if(freeBefore < CYD_WALL_CACHE_HEAP_RESERVE + bytes ||
           largestBefore < bytes)
            continue;

        CydWallCacheSlot *cache = (CydWallCacheSlot *) malloc(bytes);
        if(!cache)
            continue;

        for(int i = 0; i < slots; ++i)
        {
            cache[i].wallpic = -1;
            cache[i].lastUse = 0;
            memset(cache[i].tex, 0, sizeof(cache[i].tex));
        }
        cydWallCache = cache;
        cydWallCacheSlots = slots;
        furi_log_print_format(2, "Wolf3D",
                              "Wall texture cache %i slots, %i x %i, bytes %u, heap %u largest %u",
                              slots, CYD_WALL_CACHE_DIM, CYD_WALL_CACHE_DIM,
                              (unsigned)bytes, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return true;
    }

    cydWallCacheSkipped = true;
    furi_log_print_format(2, "Wolf3D", "Wall texture cache allocation skipped heap %u largest %u",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return false;
}

extern "C" void cyd_wall_cache_preload(void)
{
#if CYD_WOLF_FLAT_WALLS || CYD_WOLF_LOW_RES_WALL_TEXTURES
    CydEnsureWallCache();
#endif
}

static byte CydWallFallbackColor(int wallpic)
{
    if(wallpic >= DOORWALL && wallpic < PMSpriteStart)
        return 0x6d;
    return (wallpic & 1) ? 0x35 : 0x39;
}

static bool CydWallShouldTexture(int wallpic)
{
    if(wallpic >= DOORWALL && wallpic < PMSpriteStart)
        return true;                    // doors/elevator doors
    return wallpic >= CYD_WOLF_WALL_TEXTURE_MIN_PIC;
}

static CydWallCacheSlot *CydGetWallCacheSlot(int wallpic)
{
    if(wallpic < 0 || !CydWallShouldTexture(wallpic))
        return nullptr;
    if(!CydEnsureWallCache())
        return nullptr;

    int slotIndex = 0;
    for(int i = 0; i < cydWallCacheSlots; ++i)
    {
        if(cydWallCache[i].wallpic == wallpic)
        {
            cydWallCache[i].lastUse = ++cydWallCacheClock;
#if CYD_WOLF_ENABLE_PERF_LOGS
            cydWallTexHits++;
#endif
            return &cydWallCache[i];
        }
        if(cydWallCache[i].wallpic < 0)
        {
            slotIndex = i;
            goto buildSlot;
        }
        if(cydWallCache[i].lastUse < cydWallCache[slotIndex].lastUse)
            slotIndex = i;
    }

buildSlot:
    if(cydWallCache[slotIndex].wallpic == wallpic)
        return &cydWallCache[slotIndex];

#if CYD_WOLF_ENABLE_PERF_LOGS
    uint32_t buildStartUs = cyd_perf_micros();
#endif
    byte *texturePage = PM_GetTexture(wallpic);
    for(int y = 0; y < CYD_WALL_CACHE_DIM; ++y)
    {
        int sy = y * (TEXTURESIZE / CYD_WALL_CACHE_DIM);
        for(int x = 0; x < CYD_WALL_CACHE_DIM; ++x)
        {
            int sx = x * (TEXTURESIZE / CYD_WALL_CACHE_DIM);
            cydWallCache[slotIndex].tex[y * CYD_WALL_CACHE_DIM + x] =
                texturePage[sx * TEXTURESIZE + sy];
        }
    }
#if CYD_WOLF_ENABLE_PERF_LOGS
    cydWallTexBuildUs += cyd_perf_micros() - buildStartUs;
    cydWallTexBuilds++;
#endif
    cydWallCache[slotIndex].wallpic = wallpic;
    cydWallCache[slotIndex].lastUse = ++cydWallCacheClock;
    return &cydWallCache[slotIndex];
}

static byte CydWallTexel(CydWallCacheSlot *slot, int texture, int yw, byte fallback)
{
#if CYD_WOLF_ENABLE_PERF_LOGS
    cydWallTexLookups++;
#endif
    if(!slot)
        return fallback;

    int sx = (texture >> TEXTURESHIFT);
    if(sx < 0) sx = 0;
    if(sx >= TEXTURESIZE) sx = TEXTURESIZE - 1;
    if(yw < 0) yw = 0;
    if(yw >= TEXTURESIZE) yw = TEXTURESIZE - 1;

    int cx = (sx * CYD_WALL_CACHE_DIM) >> TEXTURESHIFT;
    int cy = (yw * CYD_WALL_CACHE_DIM) >> TEXTURESHIFT;
    if(cx < 0) cx = 0;
    if(cx >= CYD_WALL_CACHE_DIM) cx = CYD_WALL_CACHE_DIM - 1;
    if(cy < 0) cy = 0;
    if(cy >= CYD_WALL_CACHE_DIM) cy = CYD_WALL_CACHE_DIM - 1;
    byte col = slot->tex[cy * CYD_WALL_CACHE_DIM + cx];
    return col;
}

static void CydWallTextureFlushPerf()
{
#if CYD_WOLF_ENABLE_PERF_LOGS
    cyd_perf_record_walltex(cydWallTexLookups, cydWallTexHits, cydWallTexBuilds,
                            cydWallTexBuildUs);
    cydWallTexLookups = 0;
    cydWallTexHits = 0;
    cydWallTexBuilds = 0;
    cydWallTexBuildUs = 0;
#endif
}
#endif

void ScalePost()
{
    int ywcount, yoffs, yw, yd, yendoffs;
    byte col;
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_WALL_COLUMN_STEP > 1
    const bool cydMirrorColumn = (postx + 1) < viewwidth;
    if(cydMirrorColumn)
        wallheight[postx + 1] = wallheight[postx];
#else
    const bool cydMirrorColumn = false;
#endif

#ifdef USE_SHADING
    byte *curshades = shadetable[GetShade(wallheight[postx])];
#endif

    ywcount = yd = wallheight[postx] >> 3;
    if(yd <= 0) yd = 100;

    yoffs = (viewheight / 2 - ywcount) * vbufPitch;
    if(yoffs < 0) yoffs = 0;
    yoffs += postx;

    yendoffs = viewheight / 2 + ywcount - 1;
    yw=TEXTURESIZE-1;

    while(yendoffs >= viewheight)
    {
        ywcount -= TEXTURESIZE/2;
        while(ywcount <= 0)
        {
            ywcount += yd;
            yw--;
        }
        yendoffs--;
    }
    if(yw < 0) return;

#ifdef USE_SHADING
    col = curshades[postsource[yw]];
#else
    col = postsource[yw];
#endif
    yendoffs = yendoffs * vbufPitch + postx;

#if defined(WOLF3D_CYD_PORT) && (CYD_WOLF_FLAT_WALLS || CYD_WOLF_LOW_RES_WALL_TEXTURES)
    {
        byte basecol;
        if(lastside == 2)
            basecol = 0x6d;                 // doors: warm brown
        else if(lastside == 1)
            basecol = (lasttilehit & 1) ? 0x35 : 0x37;   // vertical walls: blue/gray family
        else
            basecol = (lasttilehit & 1) ? 0x39 : 0x3b;   // horizontal walls: nearby darker family
#ifdef USE_SHADING
        col = curshades[basecol];
#else
        col = basecol;
#endif
#if CYD_WOLF_WALL_TEXTURE_CACHE
        CydWallCacheSlot *wallSlot = CydGetWallCacheSlot(postwallpic);
#endif
        while(yoffs <= yendoffs)
        {
#if CYD_WOLF_WALL_TEXTURE_CACHE
            col = CydWallTexel(wallSlot, lasttexture, yw, basecol);
#ifdef USE_SHADING
            col = curshades[col];
#endif
#endif
            vbuf[yendoffs] = col;
            if(cydMirrorColumn)
                vbuf[yendoffs + 1] = col;
            ywcount -= TEXTURESIZE/2;
            if(ywcount <= 0)
            {
                do
                {
                    ywcount += yd;
                    yw--;
                }
                while(ywcount <= 0);
                if(yw < 0) break;
            }
            yendoffs -= vbufPitch;
        }
        return;
    }
#endif

    while(yoffs <= yendoffs)
    {
        vbuf[yendoffs] = col;
        if(cydMirrorColumn)
            vbuf[yendoffs + 1] = col;
        ywcount -= TEXTURESIZE/2;
        if(ywcount <= 0)
        {
            do
            {
                ywcount += yd;
                yw--;
            }
            while(ywcount <= 0);
            if(yw < 0) break;
#ifdef USE_SHADING
            col = curshades[postsource[yw]];
#else
            col = postsource[yw];
#endif
        }
        yendoffs -= vbufPitch;
    }
}

void GlobalScalePost(byte *vidbuf, unsigned pitch)
{
    vbuf = vidbuf;
    vbufPitch = pitch;
    ScalePost();
}

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_STATIC_DECOR_IMPOSTORS
#if CYD_WOLF_STATIC_DECOR_CACHE
namespace {
constexpr int CYD_DECOR_CACHE_DIM = 16;
constexpr int CYD_DECOR_CACHE_COUNT = 16;

struct CydDecorCacheSlot {
    int shapenum = -1;
    byte pixels[CYD_DECOR_CACHE_DIM * CYD_DECOR_CACHE_DIM];
};

CydDecorCacheSlot cydDecorCache[CYD_DECOR_CACHE_COUNT];

static void CydBuildDecorCache(int slotIndex, int shapenum)
{
    memset(cydDecorCache[slotIndex].pixels, 0, sizeof(cydDecorCache[slotIndex].pixels));

    t_compshape *shape = (t_compshape *) PM_GetSprite(shapenum);
    word *cmdptr = (word *) shape->dataofs;

    for(int i = shape->leftpix; i <= shape->rightpix; i++, cmdptr++)
    {
        if(i < 0 || i >= 64)
            continue;
        byte *line = (byte *)shape + *cmdptr;
        unsigned endy;
        while((endy = READWORD(line)) != 0)
        {
            endy >>= 1;
            short newstart = READWORD(line);
            unsigned starty = READWORD(line) >> 1;
            for(unsigned j = starty; j < endy && j < 64; j++)
            {
                byte col = ((byte *)shape)[newstart + j];
                if(!col)
                    continue;
                int cx = (i * CYD_DECOR_CACHE_DIM) >> 6;
                int cy = ((int)j * CYD_DECOR_CACHE_DIM) >> 6;
                if(cx >= 0 && cx < CYD_DECOR_CACHE_DIM && cy >= 0 && cy < CYD_DECOR_CACHE_DIM)
                    cydDecorCache[slotIndex].pixels[cy * CYD_DECOR_CACHE_DIM + cx] = col;
            }
        }
    }
}

static int CydGetDecorCacheSlot(int shapenum)
{
    for(int i = 0; i < CYD_DECOR_CACHE_COUNT; ++i)
    {
        if(cydDecorCache[i].shapenum == shapenum)
            return i;
    }
    for(int i = 0; i < CYD_DECOR_CACHE_COUNT; ++i)
    {
        if(cydDecorCache[i].shapenum < 0)
        {
            cydDecorCache[i].shapenum = shapenum;
            CydBuildDecorCache(i, shapenum);
            return i;
        }
    }
    static int victim = 0;
    int slot = victim;
    victim = (victim + 1) % CYD_DECOR_CACHE_COUNT;
    cydDecorCache[slot].shapenum = shapenum;
    CydBuildDecorCache(slot, shapenum);
    return slot;
}
}

extern "C" void CydClearDecorCache(void)
{
    for(int i = 0; i < CYD_DECOR_CACHE_COUNT; ++i)
    {
        cydDecorCache[i].shapenum = -1;
    }
}

static bool CydScaleDecorCache(int xcenter, int shapenum, unsigned height)
{
    if(shapenum < SPR_STAT_0 || shapenum > SPR_STAT_47)
        return false;

    int slotIndex = CydGetDecorCacheSlot(shapenum);

    unsigned scale = height >> 3;
    if(!scale) return true;

    int halfWidth = (int)scale;
    if(halfWidth < 2) halfWidth = 2;

    int top = viewheight / 2 - (int)scale;
    int left = xcenter - halfWidth;
    int right = xcenter + halfWidth;
    int originalLeft = left;
    int originalWidth = right - left + 1;
    if(right < 0 || left >= viewwidth) return true;
    if(left < 0) left = 0;
    if(right >= viewwidth) right = viewwidth - 1;

    top = viewheight / 2 - (int)scale;
    int bottom = viewheight / 2 + (int)scale - 1;
    int originalTop = top;
    int originalHeight = bottom - top + 1;
    if(bottom < 0 || top >= viewheight) return true;
    if(top < 0) top = 0;
    if(bottom >= viewheight) bottom = viewheight - 1;

    if(originalWidth <= 0 || originalHeight <= 0) return true;

    byte *cache = cydDecorCache[slotIndex].pixels;
    int y_step = (CYD_DECOR_CACHE_DIM * 256) / originalHeight;
    int y_start_frac = ((top - originalTop) * CYD_DECOR_CACHE_DIM * 256) / originalHeight;

    for(int x = left; x <= right; x++)
    {
        if(wallheight[x] > (int)height)
            continue;
            
        int sx = ((x - originalLeft) * CYD_DECOR_CACHE_DIM) / originalWidth;
        byte *srcCol = &cache[sx];
        
        int y_frac = y_start_frac;
        byte *dst = vbuf + top * vbufPitch + x;
        
        for(int y = top; y <= bottom; y++, dst += vbufPitch)
        {
            int sy = y_frac >> 8;
            byte col = srcCol[sy * CYD_DECOR_CACHE_DIM];
            if(col)
                *dst = col;
            y_frac += y_step;
        }
    }

    return true;
}
#endif

static byte CydDecorColor(int shapenum, unsigned height)
{
    if(shapenum >= SPR_STAT_0 && shapenum <= SPR_STAT_47)
    {
        int stat = shapenum - SPR_STAT_0;
        if(stat <= 7) return 0x2f;        // lamps/chandeliers: warm yellow
        if(stat <= 15) return 0x6d;       // tables/chairs/barrels: brown
        if(stat <= 23) return 0x4f;       // plants/columns: green/gray
        if(stat <= 31) return 0x7d;       // rubble/cages/etc: muted
        return 0x5d;
    }
    return (height > 96) ? 0x6d : 0x5d;
}

static void CydScaleDecorImpostor(int xcenter, int shapenum, unsigned height)
{
    unsigned scale = height >> 3;
    if(!scale) return;

    int halfWidth = (int)scale;
    if(halfWidth < 2) halfWidth = 2;
    if(halfWidth > 22) halfWidth = 22;

    int top = viewheight / 2 - (int)scale;
    int bottom = viewheight / 2 + (int)scale - 1;
    if(bottom < 0 || top >= viewheight) return;
    if(top < 0) top = 0;
    if(bottom >= viewheight) bottom = viewheight - 1;

    int left = xcenter - halfWidth;
    int right = xcenter + halfWidth;
    if(right < 0 || left >= viewwidth) return;
    if(left < 0) left = 0;
    if(right >= viewwidth) right = viewwidth - 1;

    byte col = CydDecorColor(shapenum, height);
    byte shade = (height > 128) ? col : (byte)(col - 1);
    int occlusionMargin = CYD_WOLF_DECOR_OCCLUSION_MARGIN;
    int dynamicMargin = (int)height >> 4;
    if(dynamicMargin > occlusionMargin)
        occlusionMargin = dynamicMargin;

    for(int x = left; x <= right; x += 2)
    {
        if((int)height <= wallheight[x] + occlusionMargin)
            continue;
        int inset = (x - left < right - x) ? x - left : right - x;
        int y0 = top + (inset >> 1);
        int y1 = bottom - (inset >> 1);
        if(y0 > y1) continue;
        byte draw = ((x + shapenum) & 2) ? col : shade;
        byte *dst = vbuf + y0 * vbufPitch + x;
        for(int y = y0; y <= y1; ++y)
        {
            *dst = draw;
            if(x + 1 <= right && (int)height > wallheight[x + 1] + occlusionMargin)
                *(dst + 1) = draw;
            dst += vbufPitch;
        }
    }
}

#if CYD_WOLF_ACTOR_IMPOSTORS
static void CydScaleActorImpostor(int xcenter, int shapenum, unsigned height)
{
    unsigned scale = height >> 3;
    if(!scale) return;

    int halfWidth = (int)scale;
    if(halfWidth < 3) halfWidth = 3;
    if(halfWidth > 24) halfWidth = 24;

    int top = viewheight / 2 - (int)scale;
    int bottom = viewheight / 2 + (int)scale - 1;
    if(bottom < 0 || top >= viewheight) return;
    if(top < 0) top = 0;
    if(bottom >= viewheight) bottom = viewheight - 1;

    int left = xcenter - halfWidth;
    int right = xcenter + halfWidth;
    if(right < 0 || left >= viewwidth) return;
    if(left < 0) left = 0;
    if(right >= viewwidth) right = viewwidth - 1;

    const int spriteHeight = bottom - top + 1;
    if(spriteHeight <= 0) return;

    byte body = (shapenum & 1) ? 0x6d : 0x5d;
    byte dark = (byte)(body - 1);
    byte face = 0x2f;
    byte weapon = 0x0f;

    int occlusionMargin = (int)height >> 5;
    if(occlusionMargin < 4) occlusionMargin = 4;

    for(int x = left; x <= right; x += 2)
    {
        if((int)height <= wallheight[x] + occlusionMargin)
            continue;

        const int relx = x - xcenter;
        const int absx = relx < 0 ? -relx : relx;
        const int columnLimit = halfWidth - (absx >> 2);
        if(absx > columnLimit)
            continue;

        byte *dst = vbuf + top * vbufPitch + x;
        for(int y = top; y <= bottom; ++y)
        {
            int rely = y - top;
            byte col;
            if(rely < spriteHeight / 4)
            {
                if(absx > halfWidth / 2)
                {
                    dst += vbufPitch;
                    continue;
                }
                col = face;
            }
            else if(rely < (spriteHeight * 3) / 4)
            {
                col = (absx > (halfWidth * 2) / 3) ? dark : body;
            }
            else
            {
                if(((x + shapenum) & 3) == 0)
                {
                    dst += vbufPitch;
                    continue;
                }
                col = dark;
            }

            if(rely > spriteHeight / 3 && rely < spriteHeight / 2 &&
               relx > 0 && relx < halfWidth)
                col = weapon;

            *dst = col;
            if(x + 1 <= right && (int)height > wallheight[x + 1] + occlusionMargin)
                *(dst + 1) = col;
            dst += vbufPitch;
        }
    }
}
#endif
#endif

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_DOWNSAMPLED_WEAPON
namespace {
constexpr int CYD_WEAPON_CACHE_DIM = CYD_WOLF_WEAPON_CACHE_DIM;

struct CydWeaponCacheSlot {
    int shapenum = -1;
    uint32_t lastUse = 0;
    byte pixels[CYD_WEAPON_CACHE_DIM * CYD_WEAPON_CACHE_DIM];
};

CydWeaponCacheSlot cydWeaponCache[CYD_WOLF_WEAPON_CACHE_SLOTS];
uint32_t cydWeaponCacheClock = 0;
}

static int CydChooseWeaponCacheSlot(int shapenum)
{
    int best = 0;
    for(int i = 0; i < CYD_WOLF_WEAPON_CACHE_SLOTS; ++i)
    {
        if(cydWeaponCache[i].shapenum == shapenum)
            return i;
        if(cydWeaponCache[i].shapenum < 0)
            return i;
        if(cydWeaponCache[i].lastUse < cydWeaponCache[best].lastUse)
            best = i;
    }
    return best;
}

static void CydBuildWeaponCache(int slotIndex, int shapenum)
{
    CydWeaponCacheSlot &slot = cydWeaponCache[slotIndex];
    memset(slot.pixels, 0, sizeof(slot.pixels));

    t_compshape *shape = (t_compshape *) PM_GetSprite(shapenum);
    word *cmdptr = (word *) shape->dataofs;

    for(int i = shape->leftpix; i <= shape->rightpix; ++i, ++cmdptr)
    {
        if(i < 0 || i >= 64)
            continue;

        byte *line = (byte *)shape + *cmdptr;
        unsigned endy;
        while((endy = READWORD(line)) != 0)
        {
            endy >>= 1;
            short newstart = READWORD(line);
            unsigned starty = READWORD(line) >> 1;
            for(unsigned j = starty; j < endy && j < 64; ++j)
            {
                byte col = ((byte *)shape)[newstart + j];
                if(!col)
                    continue;
                int cx = (i * CYD_WEAPON_CACHE_DIM) >> 6;
                int cy = ((int)j * CYD_WEAPON_CACHE_DIM) >> 6;
                if(cx >= 0 && cx < CYD_WEAPON_CACHE_DIM &&
                   cy >= 0 && cy < CYD_WEAPON_CACHE_DIM)
                    slot.pixels[cy * CYD_WEAPON_CACHE_DIM + cx] = col;
            }
        }
    }

    slot.shapenum = shapenum;
}

static bool CydScaleCachedWeapon(int xcenter, int shapenum, unsigned height)
{
    if(CYD_WOLF_WEAPON_CACHE_SLOTS <= 0)
        return false;

    int slotIndex = CydChooseWeaponCacheSlot(shapenum);
    CydWeaponCacheSlot &slot = cydWeaponCache[slotIndex];
    if(slot.shapenum != shapenum)
        CydBuildWeaponCache(slotIndex, shapenum);
    slot.lastUse = ++cydWeaponCacheClock;

    unsigned scale = height >> 1;
    if(!scale)
        return true;

    const int outSize = (int)scale * 2;
    if(outSize <= 0)
        return true;

    const int outLeft = xcenter - (int)scale;
    const int outTop = viewheight / 2 - (int)scale;
    int left = outLeft;
    int top = outTop;
    int right = outLeft + outSize - 1;
    int bottom = outTop + outSize - 1;

    if(right < 0 || bottom < 0 || left >= viewwidth || top >= viewheight)
        return true;
    if(left < 0) left = 0;
    if(top < 0) top = 0;
    if(right >= viewwidth) right = viewwidth - 1;
    if(bottom >= viewheight) bottom = viewheight - 1;

    for(int y = top; y <= bottom; ++y)
    {
        int sy = ((y - outTop) * CYD_WEAPON_CACHE_DIM) / outSize;
        if(sy < 0) sy = 0;
        if(sy >= CYD_WEAPON_CACHE_DIM) sy = CYD_WEAPON_CACHE_DIM - 1;
        byte *dst = vbuf + y * vbufPitch + left;
        for(int x = left; x <= right; x += 2)
        {
            int sx = ((x - outLeft) * CYD_WEAPON_CACHE_DIM) / outSize;
            if(sx < 0) sx = 0;
            if(sx >= CYD_WEAPON_CACHE_DIM) sx = CYD_WEAPON_CACHE_DIM - 1;
            byte col = slot.pixels[sy * CYD_WEAPON_CACHE_DIM + sx];
            if(col)
            {
                dst[x - left] = col;
                if(x + 1 <= right)
                    dst[x + 1 - left] = col;
            }
        }
    }

    return true;
}
#endif

/*
====================
=
= HitVertWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitVertWall (void)
{
    int wallpic;
    int texture;

    texture = ((yintercept+texdelta)>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;
    if (xtilestep == -1)
    {
        texture = TEXTUREMASK-texture;
        xintercept += TILEGLOBAL;
    }

    if(lastside==1 && lastintercept==xtile && lasttilehit==tilehit && !(lasttilehit & 0x40))
    {
        if((pixx&3) && texture == lasttexture)
        {
            ScalePost();
            postx = pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost();
        wallheight[pixx] = CalcHeight();
        postsource+=texture-lasttexture;
        postwidth=1;
        postx=pixx;
        lasttexture=texture;
        return;
    }

    if(lastside!=-1) ScalePost();

    lastside=1;
    lastintercept=xtile;
    lasttilehit=tilehit;
    lasttexture=texture;
    wallheight[pixx] = CalcHeight();
    postx = pixx;
    postwidth = 1;

    if (tilehit & 0x40)
    {                                                               // check for adjacent doors
        ytile = (short)(yintercept>>TILESHIFT);
        if ( tilemap[xtile-xtilestep][ytile]&0x80 )
            wallpic = DOORWALL+3;
        else
            wallpic = vertwall[tilehit & ~0x40];
    }
    else
        wallpic = vertwall[tilehit];

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_WALL_TEXTURE_CACHE
    postwallpic = wallpic;
#endif

#if defined(WOLF3D_CYD_PORT) && (CYD_WOLF_FLAT_WALLS || CYD_WOLF_LOW_RES_WALL_TEXTURES)
    postsource = nullptr;
#else
    postsource = PM_GetTexture(wallpic) + texture;
#endif
}


/*
====================
=
= HitHorizWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitHorizWall (void)
{
    int wallpic;
    int texture;

    texture = ((xintercept+texdelta)>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;
    if (ytilestep == -1)
        yintercept += TILEGLOBAL;
    else
        texture = TEXTUREMASK-texture;

    if(lastside==0 && lastintercept==ytile && lasttilehit==tilehit && !(lasttilehit & 0x40))
    {
        if((pixx&3) && texture == lasttexture)
        {
            ScalePost();
            postx=pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost();
        wallheight[pixx] = CalcHeight();
        postsource+=texture-lasttexture;
        postwidth=1;
        postx=pixx;
        lasttexture=texture;
        return;
    }

    if(lastside!=-1) ScalePost();

    lastside=0;
    lastintercept=ytile;
    lasttilehit=tilehit;
    lasttexture=texture;
    wallheight[pixx] = CalcHeight();
    postx = pixx;
    postwidth = 1;

    if (tilehit & 0x40)
    {                                                               // check for adjacent doors
        xtile = (short)(xintercept>>TILESHIFT);
        if ( tilemap[xtile][ytile-ytilestep]&0x80)
            wallpic = DOORWALL+2;
        else
            wallpic = horizwall[tilehit & ~0x40];
    }
    else
        wallpic = horizwall[tilehit];

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_WALL_TEXTURE_CACHE
    postwallpic = wallpic;
#endif

#if defined(WOLF3D_CYD_PORT) && (CYD_WOLF_FLAT_WALLS || CYD_WOLF_LOW_RES_WALL_TEXTURES)
    postsource = nullptr;
#else
    postsource = PM_GetTexture(wallpic) + texture;
#endif
}

//==========================================================================

/*
====================
=
= HitHorizDoor
=
====================
*/

void HitHorizDoor (void)
{
    int doorpage = DOORWALL;
    int doornum;
    int texture;

    doornum = tilehit&0x7f;
    texture = ((xintercept-doorposition[doornum])>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;

    if(lasttilehit==tilehit)
    {
        if((pixx&3) && texture == lasttexture)
        {
            ScalePost();
            postx=pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost();
        wallheight[pixx] = CalcHeight();
        postsource+=texture-lasttexture;
        postwidth=1;
        postx=pixx;
        lasttexture=texture;
        return;
    }

    if(lastside!=-1) ScalePost();

    lastside=2;
    lasttilehit=tilehit;
    lasttexture=texture;
    wallheight[pixx] = CalcHeight();
    postx = pixx;
    postwidth = 1;

    switch(doorobjlist[doornum].lock)
    {
        case dr_normal:
            doorpage = DOORWALL;
            break;
        case dr_lock1:
        case dr_lock2:
        case dr_lock3:
        case dr_lock4:
            doorpage = DOORWALL+6;
            break;
        case dr_elevator:
            doorpage = DOORWALL+4;
            break;
    }

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_WALL_TEXTURE_CACHE
    postwallpic = doorpage;
#endif

#if defined(WOLF3D_CYD_PORT) && (CYD_WOLF_FLAT_WALLS || CYD_WOLF_LOW_RES_WALL_TEXTURES)
    postsource = nullptr;
#else
    postsource = PM_GetTexture(doorpage) + texture;
#endif
}

//==========================================================================

/*
====================
=
= HitVertDoor
=
====================
*/

void HitVertDoor (void)
{
    int doorpage = DOORWALL+1;
    int doornum;
    int texture;

    doornum = tilehit&0x7f;
    texture = ((yintercept-doorposition[doornum])>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;

    if(lasttilehit==tilehit)
    {
        if((pixx&3) && texture == lasttexture)
        {
            ScalePost();
            postx=pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost();
        wallheight[pixx] = CalcHeight();
        postsource+=texture-lasttexture;
        postwidth=1;
        postx=pixx;
        lasttexture=texture;
        return;
    }

    if(lastside!=-1) ScalePost();

    lastside=2;
    lasttilehit=tilehit;
    lasttexture=texture;
    wallheight[pixx] = CalcHeight();
    postx = pixx;
    postwidth = 1;

    switch(doorobjlist[doornum].lock)
    {
        case dr_normal:
            doorpage = DOORWALL+1;
            break;
        case dr_lock1:
        case dr_lock2:
        case dr_lock3:
        case dr_lock4:
            doorpage = DOORWALL+7;
            break;
        case dr_elevator:
            doorpage = DOORWALL+5;
            break;
    }

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_WALL_TEXTURE_CACHE
    postwallpic = doorpage;
#endif

#if defined(WOLF3D_CYD_PORT) && (CYD_WOLF_FLAT_WALLS || CYD_WOLF_LOW_RES_WALL_TEXTURES)
    postsource = nullptr;
#else
    postsource = PM_GetTexture(doorpage) + texture;
#endif
}

//==========================================================================

#define HitHorizBorder HitHorizWall
#define HitVertBorder HitVertWall

//==========================================================================

const byte vgaCeiling[]=
{
#ifndef SPEAR
 0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0xbf,
 0x4e,0x4e,0x4e,0x1d,0x8d,0x4e,0x1d,0x2d,0x1d,0x8d,
 0x1d,0x1d,0x1d,0x1d,0x1d,0x2d,0xdd,0x1d,0x1d,0x98,

 0x1d,0x9d,0x2d,0xdd,0xdd,0x9d,0x2d,0x4d,0x1d,0xdd,
 0x7d,0x1d,0x2d,0x2d,0xdd,0xd7,0x1d,0x1d,0x1d,0x2d,
 0x1d,0x1d,0x1d,0x1d,0xdd,0xdd,0x7d,0xdd,0xdd,0xdd
#else
 0x6f,0x4f,0x1d,0xde,0xdf,0x2e,0x7f,0x9e,0xae,0x7f,
 0x1d,0xde,0xdf,0xde,0xdf,0xde,0xe1,0xdc,0x2e,0x1d,0xdc
#endif
};

/*
=====================
=
= VGAClearScreen
=
=====================
*/

void VGAClearScreen (void)
{
    byte ceiling=vgaCeiling[gamestate.episode*10+mapon];

    int y;
    byte *ptr = vbuf;
#ifdef USE_SHADING
    for(y = 0; y < viewheight / 2; y++, ptr += vbufPitch)
        memset(ptr, shadetable[GetShade((viewheight / 2 - y) << 3)][ceiling], viewwidth);
    for(; y < viewheight; y++, ptr += vbufPitch)
        memset(ptr, shadetable[GetShade((y - viewheight / 2) << 3)][0x19], viewwidth);
#else
    for(y = 0; y < viewheight / 2; y++, ptr += vbufPitch)
        memset(ptr, ceiling, viewwidth);
    for(; y < viewheight; y++, ptr += vbufPitch)
        memset(ptr, 0x19, viewwidth);
#endif
}

//==========================================================================

/*
=====================
=
= CalcRotate
=
=====================
*/

int CalcRotate (objtype *ob)
{
    int angle, viewangle;

    // this isn't exactly correct, as it should vary by a trig value,
    // but it is close enough with only eight rotations

    viewangle = player->angle + (centerx - ob->viewx)/8;

    if (ob->obclass == rocketobj || ob->obclass == hrocketobj)
        angle = (viewangle-180) - ob->angle;
    else
        angle = (viewangle-180) - dirangle[ob->dir];

    angle+=ANGLES/16;
    while (angle>=ANGLES)
        angle-=ANGLES;
    while (angle<0)
        angle+=ANGLES;

    if (ob->state->rotate == 2)             // 2 rotation pain frame
        return 0;               // pain with shooting frame bugfix

    return angle/(ANGLES/8);
}

void ScaleShape (int xcenter, int shapenum, unsigned height, uint32_t flags)
{
    t_compshape *shape;
    unsigned scale,pixheight;
    unsigned starty,endy;
    word *cmdptr;
    byte *cline;
    byte *line;
    byte *vmem;
    int actx,i,upperedge;
    short newstart;
    int scrstarty,screndy,lpix,rpix,pixcnt,ycnt;
    unsigned j;
    byte col;

#ifdef USE_SHADING
    byte *curshades;
    if(flags & FL_FULLBRIGHT)
        curshades = shadetable[0];
    else
        curshades = shadetable[GetShade(height)];
#endif

    scale=height>>3;                 // low three bits are fractional
    if(!scale) return;   // too close or far away

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_FAST_SPRITES
    const bool cydStaticDecor = (flags & CYD_VIS_STATIC_DECOR) != 0;
#if CYD_WOLF_ACTOR_IMPOSTORS
    if(flags & CYD_VIS_ACTOR)
    {
        CydScaleActorImpostor(xcenter, shapenum, height);
        return;
    }
#endif
#if CYD_WOLF_STATIC_DECOR_IMPOSTORS
    if(cydStaticDecor)
    {
#if CYD_WOLF_STATIC_DECOR_CACHE
        if(CydScaleDecorCache(xcenter, shapenum, height))
            return;
#endif
        // Fall through to full resolution ScaleShape if uncached!
    }
#endif
#if CYD_WOLF_HIDE_TINY_DECOR_SPRITES
    if(cydStaticDecor && height <= CYD_WOLF_TINY_DECOR_MAX_HEIGHT)
        return;
#endif
    const int cydFastThreshold = cydStaticDecor ? CYD_WOLF_FAST_DECOR_SPRITE_MIN_HEIGHT :
                                                  CYD_WOLF_FAST_SPRITE_MIN_HEIGHT;
    const int cydSpriteSourceStep = 1;
    const int cydSpriteColumnStep = (height >= (unsigned)cydFastThreshold) ? 2 : 1;
    const int cydSpriteRowStep = 1;
#else
    const int cydSpriteSourceStep = 1;
    const int cydSpriteColumnStep = 1;
    const int cydSpriteRowStep = 1;
#endif

    shape = (t_compshape *) PM_GetSprite(shapenum);

    pixheight=scale*SPRITESCALEFACTOR;
    actx=xcenter-scale;
    upperedge=viewheight/2-scale;

    cmdptr=(word *) shape->dataofs;

    for(i=shape->leftpix,pixcnt=i*pixheight,rpix=(pixcnt>>6)+actx;i<=shape->rightpix;i+=cydSpriteSourceStep,cmdptr+=cydSpriteSourceStep)
    {
        lpix=rpix;
        if(lpix>=viewwidth) break;
        pixcnt+=pixheight*cydSpriteSourceStep;
        rpix=(pixcnt>>6)+actx;
        if(lpix!=rpix && rpix>0)
        {
            if(lpix<0) lpix=0;
            if(rpix>viewwidth) rpix=viewwidth,i=shape->rightpix+1;
            cline=(byte *)shape + *cmdptr;
            while(lpix<rpix)
            {
                if(wallheight[lpix]<=(int)height)
                {
                    line=cline;
                    while((endy = READWORD(line)) != 0)
                    {
                        endy >>= 1;
                        newstart = READWORD(line);
                        starty = READWORD(line) >> 1;
                        j=starty;
                        ycnt=j*pixheight;
                        screndy=(ycnt>>6)+upperedge;
                        if(screndy<0) vmem=vbuf+lpix;
                        else vmem=vbuf+screndy*vbufPitch+lpix;
                        for(;j<endy;j++)
                        {
                            scrstarty=screndy;
                            ycnt+=pixheight;
                            screndy=(ycnt>>6)+upperedge;
                            if(scrstarty!=screndy && screndy>0)
                            {
#ifdef USE_SHADING
                                col=curshades[((byte *)shape)[newstart+j]];
#else
                                col=((byte *)shape)[newstart+j];
#endif
                                if(scrstarty<0) scrstarty=0;
                                if(screndy>viewheight) screndy=viewheight,j=endy;

                                while(scrstarty<screndy)
                                {
                                    *vmem=col;
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_FAST_SPRITES
                                    if(lpix + 1 < rpix && lpix + 1 < viewwidth &&
                                       wallheight[lpix + 1] <= (int)height)
                                        *(vmem + 1)=col;
#endif
                                    vmem+=vbufPitch*cydSpriteRowStep;
                                    scrstarty+=cydSpriteRowStep;
                                }
                            }
                        }
                    }
                }
                lpix += cydSpriteColumnStep;
            }
        }
    }
}

void SimpleScaleShape (int xcenter, int shapenum, unsigned height)
{
    t_compshape   *shape;
    unsigned scale,pixheight;
    unsigned starty,endy;
    word *cmdptr;
    byte *cline;
    byte *line;
    int actx,i,upperedge;
    short newstart;
    int scrstarty,screndy,lpix,rpix,pixcnt,ycnt;
    unsigned j;
    byte col;
    byte *vmem;

    scale=height>>1;
    pixheight=scale*SPRITESCALEFACTOR;
    actx=xcenter-scale;
    upperedge=viewheight/2-scale;

    shape = (t_compshape *) PM_GetSprite(shapenum);
    cmdptr=shape->dataofs;

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_FAST_WEAPON_SPRITES
    const int cydSimpleSourceStep = 2;
    const int cydSimpleColumnStep = 2;
    const int cydSimpleRowStep = 1;
#else
    const int cydSimpleSourceStep = 1;
    const int cydSimpleColumnStep = 1;
    const int cydSimpleRowStep = 1;
#endif

    for(i=shape->leftpix,pixcnt=i*pixheight,rpix=(pixcnt>>6)+actx;i<=shape->rightpix;i+=cydSimpleSourceStep,cmdptr+=cydSimpleSourceStep)
    {
        lpix=rpix;
        if(lpix>=viewwidth) break;
        pixcnt+=pixheight*cydSimpleSourceStep;
        rpix=(pixcnt>>6)+actx;
        if(lpix!=rpix && rpix>0)
        {
            if(lpix<0) lpix=0;
            if(rpix>viewwidth) rpix=viewwidth,i=shape->rightpix+1;
            cline = (byte *)shape + *cmdptr;
            while(lpix<rpix)
            {
                line=cline;
                while((endy = READWORD(line)) != 0)
                {
                    endy >>= 1;
                    newstart = READWORD(line);
                    starty = READWORD(line) >> 1;
                    j=starty;
                    ycnt=j*pixheight;
                    screndy=(ycnt>>6)+upperedge;
                    if(screndy<0) vmem=vbuf+lpix;
                    else vmem=vbuf+screndy*vbufPitch+lpix;
                    for(;j<endy;j++)
                    {
                        scrstarty=screndy;
                        ycnt+=pixheight;
                        screndy=(ycnt>>6)+upperedge;
                        if(scrstarty!=screndy && screndy>0)
                        {
                            col=((byte *)shape)[newstart+j];
                            if(scrstarty<0) scrstarty=0;
                            if(screndy>viewheight) screndy=viewheight,j=endy;

                            while(scrstarty<screndy)
                            {
                                *vmem=col;
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_FAST_WEAPON_SPRITES
                                if(lpix + 1 < rpix && lpix + 1 < viewwidth)
                                    *(vmem + 1)=col;
#endif
                                vmem+=vbufPitch*cydSimpleRowStep;
                                scrstarty+=cydSimpleRowStep;
                            }
                        }
                    }
                }
                lpix += cydSimpleColumnStep;
            }
        }
    }
}

/*
=====================
=
= DrawScaleds
=
= Draws all objects that are visable
=
=====================
*/

#ifndef CYD_WOLF_MAXVISABLE
#define CYD_WOLF_MAXVISABLE 250
#endif

#define MAXVISABLE CYD_WOLF_MAXVISABLE

typedef struct
{
    short      viewx,
               viewheight,
               shapenum;
    short      flags;          // this must be changed to uint32_t, when you
                               // you need more than 16-flags for drawing
#ifdef USE_DIR3DSPR
    statobj_t *transsprite;
#endif
} visobj_t;

visobj_t vislist[MAXVISABLE];
visobj_t *visptr,*visstep,*farthest;

void DrawScaleds (void)
{
    int      i,least,numvisable,height;
    byte     *tilespot,*visspot;
    unsigned spotloc;

    statobj_t *statptr;
    objtype   *obj;
#ifdef WOLF3D_CYD_PORT
    uint16_t cydDecorVisible = 0;
    uint16_t cydBonusVisible = 0;
    uint16_t cydActorVisible = 0;
    uint16_t cydSpritesDrawn = 0;
#if CYD_WOLF_ENABLE_PERF_LOGS
    uint32_t cydDecorDrawUs = 0;
    uint32_t cydBonusDrawUs = 0;
    uint32_t cydActorDrawUs = 0;
#endif
#endif

    visptr = &vislist[0];

//
// place static objects
//
    for (statptr = &statobjlist[0] ; statptr !=laststatobj ; statptr++)
    {
        if ((visptr->shapenum = statptr->shapenum) == -1)
            continue;                                               // object has been deleted

        if (!*statptr->visspot)
            continue;                                               // not visable

        if (TransformTile (statptr->tilex,statptr->tiley,
            &visptr->viewx,&visptr->viewheight) && statptr->flags & FL_BONUS)
        {
            GetBonus (statptr);
            if(statptr->shapenum == -1)
                continue;                                           // object has been taken
        }

        if (!visptr->viewheight)
            continue;                                               // to close to the object

#if defined(WOLF3D_CYD_PORT) && !CYD_WOLF_DRAW_STATIC_DECOR
        if(!(statptr->flags & FL_BONUS))
            continue;                                               // field mode: skip static decor until native sprite cache exists
#endif

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_HIDE_TINY_DECOR_SPRITES
        if(!(statptr->flags & FL_BONUS) && visptr->viewheight <= CYD_WOLF_TINY_DECOR_MAX_HEIGHT)
            continue;                                               // tiny far decor is not worth drawing on CYD
#endif

#ifdef USE_DIR3DSPR
        if(statptr->flags & FL_DIR_MASK)
            visptr->transsprite=statptr;
        else
            visptr->transsprite=NULL;
#endif

        if (visptr < &vislist[MAXVISABLE-1])    // don't let it overflow
        {
            visptr->flags = (short) statptr->flags;
#ifdef WOLF3D_CYD_PORT
            if(!(statptr->flags & FL_BONUS))
            {
                visptr->flags |= CYD_VIS_STATIC_DECOR;
                cydDecorVisible++;
            }
            else
            {
                visptr->flags |= CYD_VIS_BONUS;
                cydBonusVisible++;
            }
#endif
            visptr++;
        }
    }

//
// place active objects
//
    for (obj = player->next;obj;obj=obj->next)
    {
        if ((visptr->shapenum = obj->state->shapenum)==0)
            continue;                                               // no shape

        spotloc = (obj->tilex<<mapshift)+obj->tiley;   // optimize: keep in struct?
        visspot = &spotvis[0][0]+spotloc;
        tilespot = &tilemap[0][0]+spotloc;

        //
        // could be in any of the nine surrounding tiles
        //
        if (*visspot
            || ( *(visspot-1) && !*(tilespot-1) )
            || ( *(visspot+1) && !*(tilespot+1) )
            || ( *(visspot-65) && !*(tilespot-65) )
            || ( *(visspot-64) && !*(tilespot-64) )
            || ( *(visspot-63) && !*(tilespot-63) )
            || ( *(visspot+65) && !*(tilespot+65) )
            || ( *(visspot+64) && !*(tilespot+64) )
            || ( *(visspot+63) && !*(tilespot+63) ) )
        {
            obj->active = ac_yes;
            TransformActor (obj);
            if (!obj->viewheight)
                continue;                                               // too close or far away

            visptr->viewx = obj->viewx;
            visptr->viewheight = obj->viewheight;
            if (visptr->shapenum == -1)
                visptr->shapenum = obj->temp1;  // special shape

            if (obj->state->rotate)
                visptr->shapenum += CalcRotate (obj);

            if (visptr < &vislist[MAXVISABLE-1])    // don't let it overflow
            {
                visptr->flags = (short) obj->flags;
#ifdef USE_DIR3DSPR
                visptr->transsprite = NULL;
#endif
#ifdef WOLF3D_CYD_PORT
                visptr->flags |= CYD_VIS_ACTOR;
                cydActorVisible++;
#endif
                visptr++;
            }
            obj->flags |= FL_VISABLE;
        }
        else
            obj->flags &= ~FL_VISABLE;
    }

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_MAX_STATIC_DECOR_SPRITES
    visobj_t *dstvis = &vislist[0];
    uint16_t cydDecorKept = 0;
    for (visstep = &vislist[0]; visstep < visptr; visstep++)
    {
        if(visstep->flags & CYD_VIS_STATIC_DECOR)
        {
            if(CydAlwaysKeepDecorSprite(visstep->shapenum))
            {
                cydDecorKept++;
                goto keepDecorSprite;
            }
            int nearerDecor = 0;
            for (visobj_t *cmp = &vislist[0]; cmp < visptr; cmp++)
            {
                if(!(cmp->flags & CYD_VIS_STATIC_DECOR))
                    continue;
                if(CydAlwaysKeepDecorSprite(cmp->shapenum))
                    continue;
                if(cmp->viewheight > visstep->viewheight ||
                   (cmp->viewheight == visstep->viewheight && cmp < visstep))
                    nearerDecor++;
            }
            if(nearerDecor >= CYD_WOLF_MAX_STATIC_DECOR_SPRITES)
                continue;
            cydDecorKept++;
        }
keepDecorSprite:
        if(dstvis != visstep)
            *dstvis = *visstep;
        dstvis++;
    }
    visptr = dstvis;
#endif

//
// draw from back to front
//
    numvisable = (int) (visptr-&vislist[0]);

    if (!numvisable)
    {
#ifdef WOLF3D_CYD_PORT
#if CYD_WOLF_ENABLE_PERF_LOGS
        cyd_perf_record_sprites(0, 0, 0, 0, 0, 0, 0, 0);
#endif
#endif
        return;                                                                 // no visable objects
    }

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_SPRITE_BUDGET_US
    uint32_t cydSpriteBudgetStart = cyd_perf_micros();
#endif

    for (i = 0; i<numvisable; i++)
    {
        least = 32000;
        for (visstep=&vislist[0] ; visstep<visptr ; visstep++)
        {
            height = visstep->viewheight;
            if (height < least)
            {
                least = height;
                farthest = visstep;
            }
        }
        //
        // draw farthest
        //
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_ENABLE_PERF_LOGS
        uint32_t cydOneSpriteStart = cyd_perf_micros();
#endif
#ifdef USE_DIR3DSPR
        if(farthest->transsprite)
            Scale3DShape(vbuf, vbufPitch, farthest->transsprite);
        else
#endif
            ScaleShape(farthest->viewx, farthest->shapenum, farthest->viewheight, farthest->flags);

#ifdef WOLF3D_CYD_PORT
        int cydTraceCategory = 0;
        if(farthest->flags & CYD_VIS_STATIC_DECOR)
            cydTraceCategory = CYD_TRACE_SPRITE_DECOR;
        else if(farthest->flags & CYD_VIS_BONUS)
            cydTraceCategory = CYD_TRACE_SPRITE_BONUS;
        else if(farthest->flags & CYD_VIS_ACTOR)
            cydTraceCategory = CYD_TRACE_SPRITE_ACTOR;
        cyd_trace_sprite(farthest->shapenum, cydTraceCategory, farthest->viewheight);
#endif

#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_ENABLE_PERF_LOGS
        uint32_t cydOneSpriteUs = cyd_perf_micros() - cydOneSpriteStart;
        if(farthest->flags & CYD_VIS_STATIC_DECOR)
            cydDecorDrawUs += cydOneSpriteUs;
        else if(farthest->flags & CYD_VIS_BONUS)
            cydBonusDrawUs += cydOneSpriteUs;
        else if(farthest->flags & CYD_VIS_ACTOR)
            cydActorDrawUs += cydOneSpriteUs;
#endif
        cydSpritesDrawn++;
        farthest->viewheight = 32000;
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_SPRITE_BUDGET_US
        if(i > 0 && cyd_perf_micros() - cydSpriteBudgetStart > CYD_WOLF_SPRITE_BUDGET_US)
            break;
#endif
    }
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_ENABLE_PERF_LOGS
    cyd_perf_record_sprites((uint16_t)numvisable, cydSpritesDrawn,
#if CYD_WOLF_MAX_STATIC_DECOR_SPRITES
                            cydDecorKept,
#else
                            cydDecorVisible,
#endif
                            cydBonusVisible, cydActorVisible,
                            cydDecorDrawUs, cydBonusDrawUs, cydActorDrawUs);
#endif
}

//==========================================================================

/*
==============
=
= DrawPlayerWeapon
=
= Draw the player's hands
=
==============
*/

const int weaponscale[NUMWEAPONS] = {SPR_KNIFEREADY, SPR_PISTOLREADY,
    SPR_MACHINEGUNREADY, SPR_CHAINREADY};

void DrawPlayerWeapon (void)
{
    int shapenum;

#ifndef SPEAR
    if (gamestate.victoryflag)
    {
#ifndef APOGEE_1_0
        if (player->state == &states[s_deathcam] && (GetTimeCount()&32) )
            SimpleScaleShape(viewwidth/2,SPR_DEATHCAM,viewheight+1);
#endif
        return;
    }
#endif

    if (gamestate.weapon != wp_none)
    {
        shapenum = weaponscale[gamestate.weapon]+gamestate.weaponframe;
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_DOWNSAMPLED_WEAPON
        if(!CydScaleCachedWeapon(viewwidth/2, shapenum, viewheight+1))
            SimpleScaleShape(viewwidth/2,shapenum,viewheight+1);
#else
        SimpleScaleShape(viewwidth/2,shapenum,viewheight+1);
#endif
#ifdef WOLF3D_CYD_PORT
        cyd_trace_sprite(shapenum, CYD_TRACE_SPRITE_WEAPON, viewheight + 1);
#endif
    }

    if (demorecord || demoplayback)
    {
        SimpleScaleShape(viewwidth/2,SPR_DEMO,viewheight+1);
#ifdef WOLF3D_CYD_PORT
        cyd_trace_sprite(SPR_DEMO, CYD_TRACE_SPRITE_WEAPON, viewheight + 1);
#endif
    }
}

boolean crosshair = false;

void DrawCrosshair (void)
{
    if (gamestate.victoryflag)
        return;

    const int c = (gamestate.health >= 50) ? 2 : (gamestate.health >= 25) ? 6 : 4;
    const int h = (viewsize == 21 && ingame) ? screenHeight : screenHeight - scaleFactor * STATUSLINES;
    const int f = (int)scaleFactor - 1;

    for (int i = -f; i <= f; i++)
    {
            VL_Hlin (screenWidth / 2 - 2 * scaleFactor,
                     h / 2 + i,
                     4 * scaleFactor + 1,
                     c);
            VL_Vlin (screenWidth / 2 + i,
                     h / 2 - 2 * scaleFactor,
                     4 * scaleFactor + 1,
                     c);
    }
}

#ifdef WOLF3D_CYD_PORT
extern int damagecount, bonuscount;
extern "C" void cyd_hw_rgb_flash_state(int damageCount, int bonusCount);
extern "C" void cyd_hw_rgb_flash_kind(int kind, int level);
extern "C" volatile int cyd_flash_kind;
extern "C" volatile int cyd_flash_level;
extern "C" volatile uint32_t cyd_flash_until;

#ifndef CYD_WOLF_BUILD_NUMBER
#define CYD_WOLF_BUILD_NUMBER 1
#endif

static const byte cydDigitFont[10][5] =
{
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
    {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7}
};

static void CydHudPixel(int x, int y, byte color)
{
    if(x < 0 || x >= screenWidth || y < 0 || y >= screenHeight)
        return;
    vbuf[y * vbufPitch + x] = color;
}

static void CydHudBlock(int x, int y, int w, int h, byte color)
{
    if(w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if(x1 > screenWidth) x1 = screenWidth;
    if(y1 > screenHeight) y1 = screenHeight;
    for(int yy = y0; yy < y1; ++yy)
    {
        byte *dst = vbuf + yy * vbufPitch + x0;
        for(int xx = x0; xx < x1; ++xx)
            *dst++ = color;
    }
}

static void CydHudDigit(int x, int y, int digit, int scale, byte color)
{
    if(digit < 0 || digit > 9) return;
    for(int row = 0; row < 5; ++row)
    {
        byte bits = cydDigitFont[digit][row];
        for(int col = 0; col < 3; ++col)
            if(bits & (1 << (2 - col)))
                CydHudBlock(x + col * scale, y + row * scale, scale, scale, color);
    }
}

static void CydHudNumber(int x, int y, int value, int width, int scale, byte color)
{
    if(value < 0) value = 0;
    int div = 1;
    for(int i = 1; i < width; ++i)
        div *= 10;
    for(int i = 0; i < width; ++i)
    {
        CydHudDigit(x + i * (scale * 4), y, (value / div) % 10, scale, color);
        div /= 10;
    }
}

static void CydHudLetter(int x, int y, char letter, int scale, byte color)
{
    byte rows[5] = {0,0,0,0,0};
    switch(letter)
    {
        case ' ': break;
        case 'C': rows[0]=7; rows[1]=4; rows[2]=4; rows[3]=4; rows[4]=7; break;
        case 'E': rows[0]=7; rows[1]=4; rows[2]=7; rows[3]=4; rows[4]=7; break;
        case 'H': rows[0]=5; rows[1]=5; rows[2]=7; rows[3]=5; rows[4]=5; break;
        case 'A': rows[0]=7; rows[1]=5; rows[2]=7; rows[3]=5; rows[4]=5; break;
        case 'L': rows[0]=4; rows[1]=4; rows[2]=4; rows[3]=4; rows[4]=7; break;
        case 'K': rows[0]=5; rows[1]=6; rows[2]=4; rows[3]=6; rows[4]=5; break;
        case 'F': rows[0]=7; rows[1]=4; rows[2]=7; rows[3]=4; rows[4]=4; break;
        case 'I': rows[0]=7; rows[1]=2; rows[2]=2; rows[3]=2; rows[4]=7; break;
        case 'M': rows[0]=5; rows[1]=7; rows[2]=7; rows[3]=5; rows[4]=5; break;
        case 'O': rows[0]=7; rows[1]=5; rows[2]=5; rows[3]=5; rows[4]=7; break;
        case 'R': rows[0]=6; rows[1]=5; rows[2]=6; rows[3]=5; rows[4]=5; break;
        case 'S': rows[0]=7; rows[1]=4; rows[2]=7; rows[3]=1; rows[4]=7; break;
        case 'T': rows[0]=7; rows[1]=2; rows[2]=2; rows[3]=2; rows[4]=2; break;
        case 'V': rows[0]=5; rows[1]=5; rows[2]=5; rows[3]=5; rows[4]=2; break;
        case 'Y': rows[0]=5; rows[1]=5; rows[2]=2; rows[3]=2; rows[4]=2; break;
    }
    for(int row = 0; row < 5; ++row)
        for(int col = 0; col < 3; ++col)
            if(rows[row] & (1 << (2 - col)))
                CydHudBlock(x + col * scale, y + row * scale, scale, scale, color);
}

static int CydHudTextWidth(const char *text, int scale)
{
    int len = 0;
    while(text[len]) len++;
    return len ? len * 4 * scale - scale : 0;
}

static void CydHudText(int x, int y, const char *text, int scale, byte color)
{
    while(*text)
    {
        CydHudLetter(x, y, *text++, scale, color);
        x += 4 * scale;
    }
}

static void CydHudField(int centerX, int y, const char *label, int value, int width)
{
    const int labelScale = 1;
    const int valueScale = 3;
    int labelX = centerX - CydHudTextWidth(label, labelScale) / 2;
    int valueX = centerX - ((width * 4 * valueScale - valueScale) / 2);
    CydHudText(labelX, y, label, labelScale, 0x0e);
    CydHudNumber(valueX, y + 8, value, width, valueScale, 0x0f);
}

static void CydDrawNativeHud()
{
    if(viewsize == 21 && ingame)
        return;

    const int y = screenHeight - scaleFactor * STATUSLINES;
    CydHudBlock(0, y, screenWidth, screenHeight - y, 0x01);
    CydHudBlock(0, y, screenWidth, 2, bordercol);

    CydHudField(18,  y + 4, "HEALTH", gamestate.health, 3);
    CydHudField(64,  y + 4, "AMMO",   gamestate.ammo,   2);
    CydHudField(106, y + 4, "LIVES",  gamestate.lives,  1);
    CydHudField(146, y + 4, "KEY",    gamestate.keys,   1);
    CydHudField(187, y + 4, "FLOOR",  gamestate.mapon + 1, 2);
    CydHudField(229, y + 4, "SCORE",  (int)(gamestate.score % 10000), 4);

    const int buildScale = 2;
    const int buildWidth = 4 * 4 * buildScale - buildScale;
    CydHudNumber(screenWidth - buildWidth - 2, screenHeight - 5 * buildScale - 2,
                 CYD_WOLF_BUILD_NUMBER % 10000, 4, buildScale, 0x0f);
}

static void CydDrawScreenFlash()
{
    uint32_t now = SDL_GetTicks();
    int kind = (now < cyd_flash_until) ? cyd_flash_kind : 0;
    int level = (now < cyd_flash_until) ? cyd_flash_level : 0;
    byte color = 0;
    int threshold = 0;

    if(kind == 3)
    {
        color = 0x20;
        threshold = level > 30 ? 14 : 11;
    }
    else if(kind == 2)
    {
        color = 0x47;
        threshold = level > 24 ? 10 : 8;
    }
    else if(kind == 1)
    {
        color = 0x0f;
        threshold = level > 20 ? 8 : 5;
    }
    else
    {
        cyd_hw_rgb_flash_kind(0, 0);
        return;
    }

    cyd_hw_rgb_flash_kind(kind, level);

    static const byte bayer4[4][4] =
    {
        { 0,  8,  2, 10},
        {12,  4, 14,  6},
        { 3, 11,  1,  9},
        {15,  7, 13,  5}
    };

    const int hudTop = screenHeight - scaleFactor * STATUSLINES;
    const int maxY = hudTop > 0 ? hudTop : screenHeight;
    for(int y = 0; y < maxY; ++y)
    {
        byte *dst = vbuf + y * vbufPitch;
        const byte *row = bayer4[y & 3];
        for(int x = 0; x < screenWidth; ++x)
        {
            if(row[x & 3] < threshold)
                dst[x] = color;
        }
    }
}
#endif

//==========================================================================


/*
=====================
=
= CalcTics
=
=====================
*/

void CalcTics (void)
{
//
// calculate tics since last refresh for adaptive timing
//
    if (lasttimecount > (int32_t) GetTimeCount())
        lasttimecount = GetTimeCount();    // if the game was paused a LONG time

    uint32_t curtime = SDL_GetTicks();
    tics = (curtime * 7) / 100 - lasttimecount;
    if(!tics)
    {
        // wait until end of current tic
        SDL_Delay(((lasttimecount + 1) * 100) / 7 - curtime);
        tics = 1;
    }

    lasttimecount += tics;

    if (tics>MAXTICS)
        tics = MAXTICS;
}


//==========================================================================

void AsmRefresh()
{
    int32_t xstep = 0,ystep = 0;
    longword xpartial = 0,ypartial = 0;
    boolean playerInPushwallBackTile = tilemap[focaltx][focalty] == 64;

    for(pixx=0;pixx<viewwidth;pixx+=CYD_WOLF_WALL_COLUMN_STEP)
    {
        short angl=midangle+pixelangle[pixx];
        if(angl<0) angl+=FINEANGLES;
        if(angl>=3600) angl-=FINEANGLES;
        if(angl<900)
        {
            xtilestep=1;
            ytilestep=-1;
            xstep=finetangent[900-1-angl];
            ystep=-finetangent[angl];
            xpartial=xpartialup;
            ypartial=ypartialdown;
        }
        else if(angl<1800)
        {
            xtilestep=-1;
            ytilestep=-1;
            xstep=-finetangent[angl-900];
            ystep=-finetangent[1800-1-angl];
            xpartial=xpartialdown;
            ypartial=ypartialdown;
        }
        else if(angl<2700)
        {
            xtilestep=-1;
            ytilestep=1;
            xstep=-finetangent[2700-1-angl];
            ystep=finetangent[angl-1800];
            xpartial=xpartialdown;
            ypartial=ypartialup;
        }
        else if(angl<3600)
        {
            xtilestep=1;
            ytilestep=1;
            xstep=finetangent[angl-2700];
            ystep=finetangent[3600-1-angl];
            xpartial=xpartialup;
            ypartial=ypartialup;
        }
        yintercept=FixedMul(ystep,xpartial)+viewy;
        xtile=focaltx+xtilestep;
        xspot=(word)((xtile<<mapshift)+((uint32_t)yintercept>>16));
        xintercept=FixedMul(xstep,ypartial)+viewx;
        ytile=focalty+ytilestep;
        yspot=(word)((((uint32_t)xintercept>>16)<<mapshift)+ytile);
        texdelta=0;

        // Special treatment when player is in back tile of pushwall
        if(playerInPushwallBackTile)
        {
            if(    (pwalldir == di_east && xtilestep ==  1)
                || (pwalldir == di_west && xtilestep == -1))
            {
                int32_t yintbuf = yintercept - ((ystep * (64 - pwallpos)) >> 6);
                if((yintbuf >> 16) == focalty)   // ray hits pushwall back?
                {
                    if(pwalldir == di_east)
                        xintercept = (focaltx << TILESHIFT) + (pwallpos << 10);
                    else
                        xintercept = (focaltx << TILESHIFT) - TILEGLOBAL + ((64 - pwallpos) << 10);
                    yintercept = yintbuf;
                    ytile = (short) (yintercept >> TILESHIFT);
                    tilehit = pwalltile;
                    HitVertWall();
                    continue;
                }
            }
            else if((pwalldir == di_south && ytilestep ==  1)
                ||  (pwalldir == di_north && ytilestep == -1))
            {
                int32_t xintbuf = xintercept - ((xstep * (64 - pwallpos)) >> 6);
                if((xintbuf >> 16) == focaltx)   // ray hits pushwall back?
                {
                    xintercept = xintbuf;
                    if(pwalldir == di_south)
                        yintercept = (focalty << TILESHIFT) + (pwallpos << 10);
                    else
                        yintercept = (focalty << TILESHIFT) - TILEGLOBAL + ((64 - pwallpos) << 10);
                    xtile = (short) (xintercept >> TILESHIFT);
                    tilehit = pwalltile;
                    HitHorizWall();
                    continue;
                }
            }
        }

        do
        {
            if(ytilestep==-1 && (yintercept>>16)<=ytile) goto horizentry;
            if(ytilestep==1 && (yintercept>>16)>=ytile) goto horizentry;
vertentry:
            if((uint32_t)yintercept>mapheight*65536-1 || (word)xtile>=mapwidth)
            {
                if(xtile<0) xintercept=0, xtile=0;
                else if(xtile>=mapwidth) xintercept=mapwidth<<TILESHIFT, xtile=mapwidth-1;
                else xtile=(short) (xintercept >> TILESHIFT);
                if(yintercept<0) yintercept=0, ytile=0;
                else if(yintercept>=(mapheight<<TILESHIFT)) yintercept=mapheight<<TILESHIFT, ytile=mapheight-1;
                yspot=0xffff;
                tilehit=0;
                HitHorizBorder();
                break;
            }
            if(xspot>=maparea) break;
            tilehit=((byte *)tilemap)[xspot];
            if(tilehit)
            {
                if(tilehit&0x80)
                {
                    int32_t yintbuf=yintercept+(ystep>>1);
                    if((yintbuf>>16)!=(yintercept>>16))
                        goto passvert;
                    if((word)yintbuf<doorposition[tilehit&0x7f])
                        goto passvert;
                    yintercept=yintbuf;
                    xintercept=(xtile<<TILESHIFT)|0x8000;
                    ytile = (short) (yintercept >> TILESHIFT);
                    HitVertDoor();
                }
                else
                {
                    if(tilehit==64)
                    {
                        if(pwalldir==di_west || pwalldir==di_east)
                        {
	                        int32_t yintbuf;
                            int pwallposnorm;
                            int pwallposinv;
                            if(pwalldir==di_west)
                            {
                                pwallposnorm = 64-pwallpos;
                                pwallposinv = pwallpos;
                            }
                            else
                            {
                                pwallposnorm = pwallpos;
                                pwallposinv = 64-pwallpos;
                            }
                            if((pwalldir == di_east && xtile==pwallx && ((uint32_t)yintercept>>16)==pwally)
                                || (pwalldir == di_west && !(xtile==pwallx && ((uint32_t)yintercept>>16)==pwally)))
                            {
                                yintbuf=yintercept+((ystep*pwallposnorm)>>6);
                                if((yintbuf>>16)!=(yintercept>>16))
                                    goto passvert;

                                xintercept=(xtile<<TILESHIFT)+TILEGLOBAL-(pwallposinv<<10);
                                yintercept=yintbuf;
                                ytile = (short) (yintercept >> TILESHIFT);
                                tilehit=pwalltile;
                                HitVertWall();
                            }
                            else
                            {
                                yintbuf=yintercept+((ystep*pwallposinv)>>6);
                                if((yintbuf>>16)!=(yintercept>>16))
                                    goto passvert;

                                xintercept=(xtile<<TILESHIFT)-(pwallposinv<<10);
                                yintercept=yintbuf;
                                ytile = (short) (yintercept >> TILESHIFT);
                                tilehit=pwalltile;
                                HitVertWall();
                            }
                        }
                        else
                        {
                            int pwallposi = pwallpos;
                            if(pwalldir==di_north) pwallposi = 64-pwallpos;
                            if((pwalldir==di_south && (word)yintercept<(pwallposi<<10))
                                || (pwalldir==di_north && (word)yintercept>(pwallposi<<10)))
                            {
                                if(((uint32_t)yintercept>>16)==pwally && xtile==pwallx)
                                {
                                    if((pwalldir==di_south && (int32_t)((word)yintercept)+ystep<(pwallposi<<10))
                                            || (pwalldir==di_north && (int32_t)((word)yintercept)+ystep>(pwallposi<<10)))
                                        goto passvert;

                                    if(pwalldir==di_south)
                                        yintercept=(yintercept&0xffff0000)+(pwallposi<<10);
                                    else
                                        yintercept=(yintercept&0xffff0000)-TILEGLOBAL+(pwallposi<<10);
                                    xintercept=xintercept-((xstep*(64-pwallpos))>>6);
                                    xtile = (short) (xintercept >> TILESHIFT);
                                    tilehit=pwalltile;
                                    HitHorizWall();
                                }
                                else
                                {
                                    texdelta = -(pwallposi<<10);
                                    xintercept=xtile<<TILESHIFT;
                                    ytile = (short) (yintercept >> TILESHIFT);
                                    tilehit=pwalltile;
                                    HitVertWall();
                                }
                            }
                            else
                            {
                                if(((uint32_t)yintercept>>16)==pwally && xtile==pwallx)
                                {
                                    texdelta = -(pwallposi<<10);
                                    xintercept=xtile<<TILESHIFT;
                                    ytile = (short) (yintercept >> TILESHIFT);
                                    tilehit=pwalltile;
                                    HitVertWall();
                                }
                                else
                                {
                                    if((pwalldir==di_south && (int32_t)((word)yintercept)+ystep>(pwallposi<<10))
                                            || (pwalldir==di_north && (int32_t)((word)yintercept)+ystep<(pwallposi<<10)))
                                        goto passvert;

                                    if(pwalldir==di_south)
                                        yintercept=(yintercept&0xffff0000)-((64-pwallpos)<<10);
                                    else
                                        yintercept=(yintercept&0xffff0000)+((64-pwallpos)<<10);
                                    xintercept=xintercept-((xstep*pwallpos)>>6);
                                    xtile = (short) (xintercept >> TILESHIFT);
                                    tilehit=pwalltile;
                                    HitHorizWall();
                                }
                            }
                        }
                    }
                    else
                    {
                        xintercept=xtile<<TILESHIFT;
                        ytile = (short) (yintercept >> TILESHIFT);
                        HitVertWall();
                    }
                }
                break;
            }
passvert:
            *((byte *)spotvis+xspot)=1;
            xtile+=xtilestep;
            yintercept+=ystep;
            xspot=(word)((xtile<<mapshift)+((uint32_t)yintercept>>16));
        }
        while(1);
        continue;

        do
        {
            if(xtilestep==-1 && (xintercept>>16)<=xtile) goto vertentry;
            if(xtilestep==1 && (xintercept>>16)>=xtile) goto vertentry;
horizentry:
            if((uint32_t)xintercept>mapwidth*65536-1 || (word)ytile>=mapheight)
            {
                if(ytile<0) yintercept=0, ytile=0;
                else if(ytile>=mapheight) yintercept=mapheight<<TILESHIFT, ytile=mapheight-1;
                else ytile=(short) (yintercept >> TILESHIFT);
                if(xintercept<0) xintercept=0, xtile=0;
                else if(xintercept>=(mapwidth<<TILESHIFT)) xintercept=mapwidth<<TILESHIFT, xtile=mapwidth-1;
                xspot=0xffff;
                tilehit=0;
                HitVertBorder();
                break;
            }
            if(yspot>=maparea) break;
            tilehit=((byte *)tilemap)[yspot];
            if(tilehit)
            {
                if(tilehit&0x80)
                {
                    int32_t xintbuf=xintercept+(xstep>>1);
                    if((xintbuf>>16)!=(xintercept>>16))
                        goto passhoriz;
                    if((word)xintbuf<doorposition[tilehit&0x7f])
                        goto passhoriz;
                    xintercept=xintbuf;
                    yintercept=(ytile<<TILESHIFT)+0x8000;
                    xtile = (short) (xintercept >> TILESHIFT);
                    HitHorizDoor();
                }
                else
                {
                    if(tilehit==64)
                    {
                        if(pwalldir==di_north || pwalldir==di_south)
                        {
                            int32_t xintbuf;
                            int pwallposnorm;
                            int pwallposinv;
                            if(pwalldir==di_north)
                            {
                                pwallposnorm = 64-pwallpos;
                                pwallposinv = pwallpos;
                            }
                            else
                            {
                                pwallposnorm = pwallpos;
                                pwallposinv = 64-pwallpos;
                            }
                            if((pwalldir == di_south && ytile==pwally && ((uint32_t)xintercept>>16)==pwallx)
                                || (pwalldir == di_north && !(ytile==pwally && ((uint32_t)xintercept>>16)==pwallx)))
                            {
                                xintbuf=xintercept+((xstep*pwallposnorm)>>6);
                                if((xintbuf>>16)!=(xintercept>>16))
                                    goto passhoriz;

                                yintercept=(ytile<<TILESHIFT)+TILEGLOBAL-(pwallposinv<<10);
                                xintercept=xintbuf;
                                xtile = (short) (xintercept >> TILESHIFT);
                                tilehit=pwalltile;
                                HitHorizWall();
                            }
                            else
                            {
                                xintbuf=xintercept+((xstep*pwallposinv)>>6);
                                if((xintbuf>>16)!=(xintercept>>16))
                                    goto passhoriz;

                                yintercept=(ytile<<TILESHIFT)-(pwallposinv<<10);
                                xintercept=xintbuf;
                                xtile = (short) (xintercept >> TILESHIFT);
                                tilehit=pwalltile;
                                HitHorizWall();
                            }
                        }
                        else
                        {
                            int pwallposi = pwallpos;
                            if(pwalldir==di_west) pwallposi = 64-pwallpos;
                            if((pwalldir==di_east && (word)xintercept<(pwallposi<<10))
                                    || (pwalldir==di_west && (word)xintercept>(pwallposi<<10)))
                            {
                                if(((uint32_t)xintercept>>16)==pwallx && ytile==pwally)
                                {
                                    if((pwalldir==di_east && (int32_t)((word)xintercept)+xstep<(pwallposi<<10))
                                            || (pwalldir==di_west && (int32_t)((word)xintercept)+xstep>(pwallposi<<10)))
                                        goto passhoriz;

                                    if(pwalldir==di_east)
                                        xintercept=(xintercept&0xffff0000)+(pwallposi<<10);
                                    else
                                        xintercept=(xintercept&0xffff0000)-TILEGLOBAL+(pwallposi<<10);
                                    yintercept=yintercept-((ystep*(64-pwallpos))>>6);
                                    ytile = (short) (yintercept >> TILESHIFT);
                                    tilehit=pwalltile;
                                    HitVertWall();
                                }
                                else
                                {
                                    texdelta = -(pwallposi<<10);
                                    yintercept=ytile<<TILESHIFT;
                                    xtile = (short) (xintercept >> TILESHIFT);
                                    tilehit=pwalltile;
                                    HitHorizWall();
                                }
                            }
                            else
                            {
                                if(((uint32_t)xintercept>>16)==pwallx && ytile==pwally)
                                {
                                    texdelta = -(pwallposi<<10);
                                    yintercept=ytile<<TILESHIFT;
                                    xtile = (short) (xintercept >> TILESHIFT);
                                    tilehit=pwalltile;
                                    HitHorizWall();
                                }
                                else
                                {
                                    if((pwalldir==di_east && (int32_t)((word)xintercept)+xstep>(pwallposi<<10))
                                            || (pwalldir==di_west && (int32_t)((word)xintercept)+xstep<(pwallposi<<10)))
                                        goto passhoriz;

                                    if(pwalldir==di_east)
                                        xintercept=(xintercept&0xffff0000)-((64-pwallpos)<<10);
                                    else
                                        xintercept=(xintercept&0xffff0000)+((64-pwallpos)<<10);
                                    yintercept=yintercept-((ystep*pwallpos)>>6);
                                    ytile = (short) (yintercept >> TILESHIFT);
                                    tilehit=pwalltile;
                                    HitVertWall();
                                }
                            }
                        }
                    }
                    else
                    {
                        yintercept=ytile<<TILESHIFT;
                        xtile = (short) (xintercept >> TILESHIFT);
                        HitHorizWall();
                    }
                }
                break;
            }
passhoriz:
            *((byte *)spotvis+yspot)=1;
            ytile+=ytilestep;
            xintercept+=xstep;
            yspot=(word)((((uint32_t)xintercept>>16)<<mapshift)+ytile);
        }
        while(1);
    }
}

/*
====================
=
= WallRefresh
=
====================
*/

void WallRefresh (void)
{
    xpartialdown = viewx&(TILEGLOBAL-1);
    xpartialup = TILEGLOBAL-xpartialdown;
    ypartialdown = viewy&(TILEGLOBAL-1);
    ypartialup = TILEGLOBAL-ypartialdown;

    min_wallheight = viewheight;
    lastside = -1;                  // the first pixel is on a new wall
    AsmRefresh ();
    ScalePost ();                   // no more optimization on last post
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_WALL_TEXTURE_CACHE
    CydWallTextureFlushPerf();
#endif
}

void CalcViewVariables()
{
    viewangle = player->angle;
    midangle = viewangle*(FINEANGLES/ANGLES);
    viewsin = sintable[viewangle];
    viewcos = costable[viewangle];
    viewx = player->x - FixedMul(focallength,viewcos);
    viewy = player->y + FixedMul(focallength,viewsin);

    focaltx = (short)(viewx>>TILESHIFT);
    focalty = (short)(viewy>>TILESHIFT);

    viewtx = (short)(player->x >> TILESHIFT);
    viewty = (short)(player->y >> TILESHIFT);
}

//==========================================================================

/*
========================
=
= ThreeDRefresh
=
========================
*/

void    ThreeDRefresh (void)
{
#ifdef WOLF3D_CYD_PORT
    cyd_sound_poll();
#if CYD_WOLF_ENABLE_PERF_LOGS
    uint32_t cydRenderStart = cyd_perf_micros();
    uint32_t cydPrepUs = 0;
    uint32_t cydClearUs = 0;
    uint32_t cydWallUs = 0;
    uint32_t cydSpriteUs = 0;
    uint32_t cydWeaponUs = 0;
#endif
#if CYD_WOLF_ENABLE_FRAME_HEARTBEAT
    static uint32_t cydLastFrameLog = 0;
    uint32_t cydNow = SDL_GetTicks();
    if(cydNow - cydLastFrameLog > 1000)
    {
        cydLastFrameLog = cydNow;
        furi_log_print_format(2, "Wolf3D", "Frame heartbeat %u", (unsigned)frameon);
    }
#endif
#endif
//
// clear out the traced array
//
    memset(spotvis,0,maparea);
    spotvis[player->tilex][player->tiley] = 1;       // Detect all sprites over player fix

    vbuf = VL_LockSurface(screenBuffer);
    if(vbuf == NULL) return;

    vbuf += screenofs;
    vbufPitch = bufferPitch;

    CalcViewVariables();
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_ENABLE_PERF_LOGS
    cydPrepUs = cyd_perf_micros() - cydRenderStart;
    uint32_t cydPhaseStart = cyd_perf_micros();
#endif

//
// follow the walls from there to the right, drawing as we go
//
    VGAClearScreen ();
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_ENABLE_PERF_LOGS
    cydClearUs = cyd_perf_micros() - cydPhaseStart;
    cydPhaseStart = cyd_perf_micros();
#endif
#if defined(USE_FEATUREFLAGS) && defined(USE_STARSKY)
    if(GetFeatureFlags() & FF_STARSKY)
        DrawStarSky(vbuf, vbufPitch);
#endif

    WallRefresh ();
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_ENABLE_PERF_LOGS
    cydWallUs = cyd_perf_micros() - cydPhaseStart;
    cydPhaseStart = cyd_perf_micros();
#endif

#if defined(USE_FEATUREFLAGS) && defined(USE_PARALLAX)
    if(GetFeatureFlags() & FF_PARALLAXSKY)
        DrawParallax(vbuf, vbufPitch);
#endif
#if defined(USE_FEATUREFLAGS) && defined(USE_CLOUDSKY)
    if(GetFeatureFlags() & FF_CLOUDSKY)
        DrawClouds(vbuf, vbufPitch, min_wallheight);
#endif
#ifdef USE_FLOORCEILINGTEX
    DrawFloorAndCeiling(vbuf, vbufPitch, min_wallheight);
#endif

//
// draw all the scaled images
//
#if !defined(WOLF3D_CYD_PORT) || CYD_WOLF_DRAW_SPRITES
    DrawScaleds();                  // draw scaled stuff
#endif
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_ENABLE_PERF_LOGS
    cydSpriteUs = cyd_perf_micros() - cydPhaseStart;
    cydPhaseStart = cyd_perf_micros();
#endif

#if defined(USE_FEATUREFLAGS) && defined(USE_RAIN)
    if(GetFeatureFlags() & FF_RAIN)
        DrawRain(vbuf, vbufPitch);
#endif
#if defined(USE_FEATUREFLAGS) && defined(USE_SNOW)
    if(GetFeatureFlags() & FF_SNOW)
        DrawSnow(vbuf, vbufPitch);
#endif

#if !defined(WOLF3D_CYD_PORT) || CYD_WOLF_DRAW_WEAPON
    DrawPlayerWeapon ();    // draw player's hands
#endif
#ifdef WOLF3D_CYD_PORT
#if CYD_WOLF_SCREEN_FLASHES
    CydDrawScreenFlash();
#endif
    CydDrawNativeHud();
#endif
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_ENABLE_PERF_LOGS
    cydWeaponUs = cyd_perf_micros() - cydPhaseStart;
#endif
    if (crosshair)
        DrawCrosshair ();

    if(Keyboard[sc_Tab] && viewsize == 21 && gamestate.weapon != wp_none)
        ShowActStatus();

    VL_UnlockSurface(screenBuffer);
    vbuf = NULL;

//
// show screen and time last cycle
//

    if (fizzlein)
    {
        FizzleFade(screenBuffer, 0, 0, screenWidth, screenHeight, 20, false);
        fizzlein = false;

        lasttimecount = GetTimeCount();          // don't make a big tic count
    }
    else
    {
#ifndef REMDEBUG
        if (fpscounter)
        {
            fontnumber = 0;
            SETFONTCOLOR(7,127);
            PrintX=4; PrintY=1;
            VWB_Bar(0,0,50,10,bordercol);
            US_PrintSigned(fps);
            US_Print(" fps");
        }
#endif
        SDL_BlitSurface(screenBuffer, NULL, screen, NULL);
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_ENABLE_PERF_LOGS
        uint32_t cydRenderUs = cyd_perf_micros() - cydRenderStart;
        uint32_t cydPresentStart = cyd_perf_micros();
#endif
        VL_Flip();
#ifdef WOLF3D_CYD_PORT
        cyd_trace_frame();
#endif
#if defined(WOLF3D_CYD_PORT) && CYD_WOLF_ENABLE_PERF_LOGS
        uint32_t cydPresentUs = cyd_perf_micros() - cydPresentStart;
        cyd_perf_record_render_phases(cydPrepUs, cydClearUs, cydWallUs, cydSpriteUs, cydWeaponUs,
                                      cydPresentUs);
#endif
    }

#ifndef REMDEBUG
    if (fpscounter)
    {
        fps_frames++;
        fps_time+=tics;

        if(fps_time>35)
        {
            fps_time-=35;
            fps=fps_frames<<1;
            fps_frames=0;
        }
    }
#endif
}
