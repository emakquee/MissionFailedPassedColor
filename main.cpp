// Mission Failed / Passed Color
// AML plugin for GTA San Andreas Android 2.10 (arm64-v8a, 64 bit).
//
// Changes ONLY the colour of the "MISSION FAILED!" and "MISSION PASSED!" big messages.
// Wasted/Busted, mission titles, subtitles, help text, radar and all other HUD
// elements keep their original colours.
//
// How it works (verified against libGTASA.so 2.10 arm64):
//   * Both texts are drawn by CHud::DrawSuccessFailedMessage() from
//     CHud::m_BigMessage[0] (message style 0). That function sets the font colour with
//     CHudColours::GetRGB(HudColour, 7, alpha).
//   * Since other messages use the same function and palette slot, we first check that
//     the text on screen is really one of the configured GXT labels
//     (default: M_FAIL for Failed; M_PASS, M_PASSD, M_PASSR, M_PASSS for Passed). Passed messages
//     contain a number (~1~ = money), so labels are matched by their text *before* the
//     first number placeholder.
//   * Only then HudColour[7] is swapped for the configured colour while the original
//     function runs, and restored right afterwards.
//   * Android renders text deferred (CFont::RenderFontBuffer) and colour tags such as ~r~
//     are resolved there, so they would override our colour. Colour tags are therefore
//     removed from the on-screen copy of the matched message (never from the GXT table).

#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#include <aml-psdk/game_sa/plugin.h>
#include <aml-psdk/game_sa/engine/Hud.h>
#include <aml-psdk/game_sa/engine/Text.h>

#include <string.h>

MYMODCFG(net.psdk.missionfailedcolor, Mission Failed Passed Color, 1.1.0, Author)
NEEDGAME(com.rockstargames.gtasa)

// Palette slot used by CHud::DrawSuccessFailedMessage ("orr w1, wzr, #7" before GetRGB).
static constexpr int BIG_MESSAGE_PALETTE_SLOT = 7;
// PRINT_BIG style 1 (script) == message style 0 in the code.
static constexpr int BIG_MESSAGE_STYLE_MIDDLE = 0;

static constexpr int MAX_LABELS    = 8;
static constexpr int MAX_LABEL_LEN = 16;
static constexpr int MIN_PREFIX    = 3; // ignore labels whose text before ~1~ is shorter than this

struct Rule
{
    bool  enabled = false;
    CRGBA color   = CRGBA(255, 255, 255, 255);
    char  labels[MAX_LABELS][MAX_LABEL_LEN] = {};
    int   numLabels = 0;
};
static Rule g_Failed, g_Passed;
static bool g_Debug = false;

// ---------------------------------------------------------------- GXT helpers

static inline bool IsColourTagLetter(unsigned short c)
{
    switch (c | 0x20) // to lower case
    {
        case 'b': case 'g': case 'h': case 'l':
        case 'p': case 'r': case 's': case 'w': case 'y':
            return true;
    }
    return false;
}

// Copy without ~r~ / ~g~ / ~b~ / ~w~ / ~y~ / ... colour tags, but ONLY on the first line
// (up to the first ~n~). Later lines ("RESPECT +", "$1000") keep their tags, so they keep
// their original colour. ~n~, buttons and numbers always stay.
static void CopyWithoutColourTags(const unsigned short* src, unsigned short* dst, int cap)
{
    int o = 0;
    bool firstLine = true;
    for (int i = 0; src[i] && o < cap - 1; ++i)
    {
        if (src[i] == '~' && (src[i + 1] | 0x20) == 'n' && src[i + 2] == '~')
            firstLine = false;
        if (firstLine && src[i] == '~' && IsColourTagLetter(src[i + 1]) && src[i + 2] == '~')
        {
            i += 2;
            continue;
        }
        dst[o++] = src[i];
    }
    dst[o] = 0;
}

// Length of the text before the first number placeholder ~1~ .. ~6~.
static int LengthBeforeNumber(const unsigned short* s)
{
    int i = 0;
    for (; s[i]; ++i)
        if (s[i] == '~' && s[i + 1] >= '1' && s[i + 1] <= '6' && s[i + 2] == '~')
            break;
    return i;
}

// Does the shown text start with the text of GXT `label` (up to its first number)?
static bool ShownStartsWithLabel(const unsigned short* shownNoTags, const char* label)
{
    GxtChar* gxt = TheText.Get(label);
    if (!gxt) return false;

    unsigned short ref[MAX_BIG_MESSAGE_LENGTH];
    CopyWithoutColourTags(AsUINT(gxt), ref, MAX_BIG_MESSAGE_LENGTH);

    int n = LengthBeforeNumber(ref);
    if (n < MIN_PREFIX) return false;

    for (int i = 0; i < n; ++i)
        if (shownNoTags[i] != ref[i]) return false;
    return true;
}

static bool MatchesRule(const Rule& r, const unsigned short* shownNoTags)
{
    if (!r.enabled) return false;
    for (int i = 0; i < r.numLabels; ++i)
        if (ShownStartsWithLabel(shownNoTags, r.labels[i])) return true;
    return false;
}

static void DebugLog(const unsigned short* text, const char* result)
{
    static char last[MAX_BIG_MESSAGE_LENGTH + 16];
    char cur[MAX_BIG_MESSAGE_LENGTH + 16];
    int o = 0;
    for (int i = 0; text[i] && o < MAX_BIG_MESSAGE_LENGTH; ++i)
        cur[o++] = (text[i] < 128) ? (char)text[i] : '?';
    cur[o] = 0;
    strncat(cur, result, 15);
    if (strcmp(cur, last) == 0) return;
    strncpy(last, cur, sizeof(last) - 1);
    logger->Info("Big message on screen: \"%s\" -> %s", cur, result);
}

// Decides which colour (if any) applies to the big message now shown.
// On a match the on-screen copy is replaced by its colour-tag-free version.
static const Rule* FindMatchingRule()
{
    unsigned short* shown = AsUINT(&CHud::m_BigMessage[BIG_MESSAGE_STYLE_MIDDLE][0]);
    if (!shown[0]) return nullptr;

    unsigned short cur[MAX_BIG_MESSAGE_LENGTH];
    CopyWithoutColourTags(shown, cur, MAX_BIG_MESSAGE_LENGTH);

    const Rule* hit = nullptr;
    if (MatchesRule(g_Failed, cur))      hit = &g_Failed;
    else if (MatchesRule(g_Passed, cur)) hit = &g_Passed;

    if (g_Debug) DebugLog(shown, hit == &g_Failed ? " [FAILED]" : hit == &g_Passed ? " [PASSED]" : " [no match]");
    if (!hit) return nullptr;

    for (int i = 0; i < MAX_BIG_MESSAGE_LENGTH; ++i)
    {
        shown[i] = cur[i];
        if (!cur[i]) break;
    }
    return hit;
}

// ----------------------------------------------------------------------- hook

DECL_HOOKv(CHud_DrawSuccessFailedMessage, void)
{
    const Rule* rule = FindMatchingRule();
    if (!rule)
    {
        CHud_DrawSuccessFailedMessage();
        return;
    }

    CRGBA& slot = HudColour.value[BIG_MESSAGE_PALETTE_SLOT];
    const CRGBA saved = slot;
    slot.r = rule->color.r;
    slot.g = rule->color.g;
    slot.b = rule->color.b; // alpha is left alone, the game fades it itself

    CHud_DrawSuccessFailedMessage();

    slot = saved;
}

// ------------------------------------------------------------------ mod entry

static unsigned char ClampByte(int v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

static void LoadRule(Rule& r, const char* section, bool enabled, int R, int G, int B, const char* labels)
{
    r.enabled = cfg->Bind("Enabled", enabled, section)->GetBool();
    r.color.r = ClampByte(cfg->Bind("R", R, section)->GetInt());
    r.color.g = ClampByte(cfg->Bind("G", G, section)->GetInt());
    r.color.b = ClampByte(cfg->Bind("B", B, section)->GetInt());

    char buf[128];
    strncpy(buf, cfg->Bind("Labels", labels, section)->GetString(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    r.numLabels = 0;
    for (char* tok = strtok(buf, ", "); tok && r.numLabels < MAX_LABELS; tok = strtok(nullptr, ", "))
    {
        strncpy(r.labels[r.numLabels], tok, MAX_LABEL_LEN - 1);
        r.labels[r.numLabels][MAX_LABEL_LEN - 1] = 0;
        ++r.numLabels;
    }
}

ON_MOD_LOAD()
{
    logger->SetTag("MissionFailedColor");

    // Config (created on first run): Android/data/<package>/files/configs/<guid>.ini
    //   [MissionFailed] Enabled, R, G, B, Labels      [MissionPassed] same keys
    //   [Preferences]   Debug = 1 logs the text of every big message and whether it matched
    LoadRule(g_Failed, "MissionFailed", true, 0, 200, 255, "M_FAIL");
    LoadRule(g_Passed, "MissionPassed", true, 0, 255, 0,   "M_PASS,M_PASSD,M_PASSR,M_PASSS");
    g_Debug = cfg->Bind("Debug", false)->GetBool();
    cfg->Save();

    void* gta = GetMainLibrary();
    void* fn  = gta ? (void*)aml->GetSym(gta, "_ZN4CHud24DrawSuccessFailedMessageEv") : nullptr;
    if (!fn)
    {
        logger->Error("CHud::DrawSuccessFailedMessage not found - plugin disabled");
        return;
    }
    aml->Hook(fn, (void*)&HookOf_CHud_DrawSuccessFailedMessage, (void**)&CHud_DrawSuccessFailedMessage);
    logger->Info("Hooked. Failed=%d (%d,%d,%d) Passed=%d (%d,%d,%d)",
                 g_Failed.enabled, g_Failed.color.r, g_Failed.color.g, g_Failed.color.b,
                 g_Passed.enabled, g_Passed.color.r, g_Passed.color.g, g_Passed.color.b);
}
