#pragma once

// The pattern library: bass lines, arps and plucks, chords (pads, stabs and gates) and lead
// hooks, organised by mood and style. Every pattern is original, written from the conventions of
// the genre (offbeat and rolling basses, pedal and 3-3-2 arps, trance gates, anthem hooks), not
// transcribed from records.
//
// A family is one idea at three energies (low for intros and breakdowns, mid for grooves and
// the start of builds, high for drops) plus a fill bar for the end of each 8-bar phrase, so a
// song keeps one cohesive bass, arp, chord rhythm and hook from the intro to the last drop while
// its density follows the arrangement.
//
// Patterns are written chord-relative, a character per 16th, so the same family fits any
// progression and key:
//   r t f s   the chord's root, third, fifth, seventh (the octave on a triad)
//   R T F S   the same an octave up            l    the root an octave down
//   9 4       the ninth / the fourth above the root (in the key)
//   u d       a scale step up / down from the last note
//   k         the last note again              n N  a scale / half step leading to the next chord's root
//   a A b     the next arp tone (the family's order) / the same an octave up / the one before
//   c C o     chords: the voiced chord / with its top note doubled an octave up / root and fifth only
//   ,         a ghost: the last note, soft and short
//   -         hold the last note                .    rest
// Spaces are ignored; '|' separates bars (a family can be two bars long).
//
// Lead hooks move to the nearest instance of each chord tone, so they voice-lead smoothly over
// the changes; bass, arps and chords sit in their own registers.

#include <juce_core/juce_core.h>
#include <array>
#include <string>
#include <vector>

namespace patterns
{
    enum class Mood  { Euphoric, Hopeful, Melancholic, Dark, Driving, Dreamy, Epic, count };
    enum class Style { Uplifting, Classic, Progressive, Tech, Psy, Vocal, Orchestral, count };
    enum class Part  { Bass, Arp, Chords, Lead };

    inline const juce::StringArray& moodNames()
    {
        static const juce::StringArray n { "Euphoric", "Hopeful", "Melancholic", "Dark", "Driving", "Dreamy", "Epic" };
        return n;
    }
    inline const juce::StringArray& styleNames()
    {
        static const juce::StringArray n { "Uplifting", "Classic 138", "Progressive", "Tech trance", "Psy-leaning", "Vocal", "Orchestral" };
        return n;
    }

    /** Arp orders (for 'a'). */
    enum Order { up = 0, down, upDown, converge, skip };

    struct Family
    {
        const char* name;
        Part part;
        int kind;                    // bass: 0 long, 1 offbeat/bouncy, 2 rolling; arp: 0 melodic, 1 16ths, 2 syncopated; chords: 0 pad, 1 stabs, 2 gate; lead: 0
        uint32_t moods, styles;      // bit per Mood / Style
        std::array<const char*, 3> level;
        const char* fill;            // the phrase-end bar(s)
        float gate = 0.8f;           // a note's length as a share of the gap to the next one
        int order = up;
    };

    namespace tags
    {
        constexpr uint32_t m (Mood x) { return 1u << (int) x; }
        constexpr uint32_t s (Style x) { return 1u << (int) x; }
        constexpr uint32_t Eu = m (Mood::Euphoric), Ho = m (Mood::Hopeful), Me = m (Mood::Melancholic), Da = m (Mood::Dark),
                           Dr = m (Mood::Driving), Dm = m (Mood::Dreamy), Ep = m (Mood::Epic), allM = 0x7f;
        constexpr uint32_t Up = s (Style::Uplifting), Cl = s (Style::Classic), Pr = s (Style::Progressive), Te = s (Style::Tech),
                           Ps = s (Style::Psy), Vo = s (Style::Vocal), Or = s (Style::Orchestral), allS = 0x7f;
    }

    inline const std::vector<Family>& library()
    {
        using namespace tags;
        using P = Part;
        static const std::vector<Family> lib
        {
            //======================================================================== BASS
            // Long: breakdowns, intros, and the warm end of progressive
            { "Sub sustain",        P::Bass, 0, Dm | Me | Ep | Ho, allS,
              { "r--- ---- ---- ----", "r--- ---- r--- ----", "r--- ---- r--- --r-" }, "r--- ---- r--- n---", 1.0f },
            { "Half-note pulse",    P::Bass, 0, Me | Dm | Ho, Pr | Or | Vo,
              { "r--- ---- ---- ----", "r--- ---- f--- ----", "r--- ---- f--- r---" }, "r--- ---- u--- n---", 1.0f },
            { "Orchestral roots",   P::Bass, 0, Ep | Me | Ho, Or | Up,
              { "r--- ---- ---- ----", "r--- ---- ---- l---", "r--- ---r ---- l---" }, "r--- ---- n--- N---", 1.0f },
            { "Pedal breath",       P::Bass, 0, Da | Dm | Dr, Pr | Ps | Te,
              { "r--- ---- ---- ----", "r-.. r-.. r-.. r-..", "r-.r r-.r r-.r r-.r" }, "r-.r r-.r u-.u n-.N", 0.9f },
            // Offbeat and bouncy: the uplifting and vocal staple
            { "Classic offbeat",    P::Bass, 1, Eu | Ho | Ep, Up | Cl | Vo,
              { "..r. ..r. ..r. ..r.", "..r. ..r. ..r. ..R.", "..r. ..R. ..r. ..R." }, "..r. ..R. ..f. ..n.", 0.8f },
            { "Octave bounce",      P::Bass, 1, Eu | Ho | Ep, Up | Cl,
              { "..r. ..r. ..r. ..r.", "..r. ..R. ..r. ..R.", "..rR ..rR ..rR ..rR" }, "..rR ..rR ..fF ..Rn", 0.7f },
            { "Long offbeat pump",  P::Bass, 1, Me | Dm | Ho, Pr | Vo,
              { ".r-- .r-- .r-- .r--", ".r-- .r-- .r-- .r.R", ".r-R .r-R .r-R .r-R" }, ".r-- .r-- .f-- .n.N", 0.9f },
            { "Progressive syncopation", P::Bass, 1, Me | Dm | Da, Pr,
              { "..r. ...r ..r. ....", "..r. .r.r ..r. .r..", "..rr .r.r ..rr .r.R" }, "..rr .r.r ..ff .n.n", 0.75f },
            { "Walking offbeat",    P::Bass, 1, Ho | Me, Cl | Vo,
              { "..r. ..r. ..r. ..r.", "..r. ..r. ..t. ..f.", "..r. ..R. ..t. ..n." }, "..r. ..u. ..u. ..n.", 0.8f },
            { "Dark octave",        P::Bass, 1, Da | Dr, Te | Cl,
              { "..r. ..r. ..r. ..r.", "..r. ..l. ..r. ..l.", "..rl ..rl ..rl ..rl" }, "..rl ..rl ..rr ..N.", 0.7f },
            { "Dreamy offbeat",     P::Bass, 1, Dm | Ho, Pr | Vo | Or,
              { "..r- ---- ..r- ----", "..r- ..r- ..r- ..r-", "..r- ..f- ..r- ..t-" }, "..r- ..u- ..u- ..n-", 0.95f },
            { "Melancholic walk",   P::Bass, 1, Me, Vo | Pr | Cl,
              { "..r. ..r. ..r. ..r.", "..r. ..r. ..r. ..d.", "..r. ..R. ..r. ..d." }, "..r. ..d. ..d. ..n.", 0.8f },
            // Rolling: drops with drive
            { "Rolling 16ths",      P::Bass, 2, Dr | Eu | Ep | Da, Cl | Te | Up,
              { ".r.r .r.r .r.r .r.r", ".rrr .rrr .rrr .rrr", ".rrr .rRr .rrr .rRr" }, ".rrr .RRR .fff .nnn", 0.7f },
            { "Psy gallop",         P::Bass, 2, Da | Dr, Ps,
              { ".rr. .rr. .rr. .rr.", ".rrr .rrr .rrr .rrr", ".rrr .rrr .rrr .rRr" }, ".rrr .rrr .uuu .ddd", 0.6f },
            { "Galloping",          P::Bass, 2, Dr | Ep | Eu, Up | Cl | Te,
              { "..r. ..r. ..r. ..r.", "..rr ..rr ..rr ..rr", "..rr ..rR ..rr ..rR" }, "..rr ..rR ..ff ..nN", 0.7f },
            { "Tech bounce",        P::Bass, 2, Dr | Da, Te,
              { "..r. ..r. ..r. ..r.", ".r.r ..r. .r.r ..r.", ".rr. .r.r .rr. .r.R" }, ".rr. .r.r .ff. .n.N", 0.65f },
            { "Acid line",          P::Bass, 2, Da | Dr, Ps | Te,
              { ".r.r .r.r .r.r .r.r", ".rRr .rRr .rRr .rRr", ".rRr .tRr .rRf .rRr" }, ".rRr .uRu .dRd .nRN", 0.55f },
            { "Euphoric roll",      P::Bass, 2, Eu | Ho | Ep, Up | Vo,
              { "..r. ..r. ..r. ..r.", ".rrr .rrr .rrr .rrr", ".rrr .rrR .rrr .rRR" }, ".rrr .rrR .fff .fnN", 0.7f },
            { "Epic gallop",        P::Bass, 2, Ep, Or | Up,
              { "..r. ..r. ..r. ..r.", "..rr ..rr ..rr ..rr", "..rr ..RR ..rr ..RR" }, "..rr ..RR ..ff ..nn", 0.7f },
            { "Driving 8ths",       P::Bass, 2, Dr, Te | Cl,
              { "..r. ..r. ..r. ..r.", ".rr. .rr. .rr. .rr.", ".rrr .rrr .rrr .rrR" }, ".rrr .rrr .uuu .nnn", 0.7f },

            //======================================================================== ARPS AND PLUCKS
            // Melodic, slower: intros, breakdowns
            { "Dreamy 8ths",        P::Arp, 0, Dm | Me, Pr | Vo,
              { "a-a- a-a- a-a- a-a-", "a-a- a-a- a-aa a-a-", "a-aa a-aa a-aa a-aa" }, "a-a- a-a- A-A- A-A-", 1.0f, upDown },
            { "Sus shimmer",        P::Arp, 0, Dm | Ho, Pr | Vo | Or,
              { "r.9. f.9. r.9. f.9.", "r9f9 R9f9 r9f9 R9f9", "r9fR 9fR9 r9fR 9fRF" }, "r9fR 9f4f r9fR 4f9r", 0.9f },
            { "Anthem 8ths",        P::Arp, 0, Ep | Eu, Up | Vo | Or,
              { "a.a. a.a. a.a. a.a.", "a.a. a.a. a.a. a.aa", "a.aa a.aa a.aa a.aa" }, "a.aa a.aa A.AA A.AA", 0.9f, up },
            { "Octave ping",        P::Arp, 0, Dm | Da | Dr, Pr | Te,
              { "r... R... r... R...", "r.R. r.R. r.R. r.R.", "rR.R rR.R rR.R rR.R" }, "rRrR rRrR fFfF tTtT", 0.6f },
            // 16ths
            { "Classic 16th up",    P::Arp, 1, Eu | Ep | Ho, Up | Cl | Vo,
              { "a.a. a.a. a.a. a.a.", "aaaa aaaa aaaa aaaa", "aaaa aaaa aaaa aaAa" }, "aaaa aaaa AAAA AAAA", 0.6f, up },
            { "Pedal arp",          P::Arp, 1, Eu | Ep | Dr, Up | Cl,
              { "r.t. r.f. r.t. r.f.", "rtrf rTrF rtrf rTrF", "rtrf rTrF RtRf RTRF" }, "rTrF RTRF rTrF RTRS", 0.6f },
            { "Down cascade",       P::Arp, 1, Me | Dm | Ho, Up | Pr | Vo,
              { "a.a. a.a. a.a. a.a.", "a.aa a.aa a.aa a.aa", "aaaa aaaa aaaa aaaa" }, "aaaa aaaa aaaa bbbb", 0.65f, down },
            { "Up-down wave",       P::Arp, 1, Dm | Ho | Eu, Up | Pr | Vo | Cl | Or,
              { "a.a. a.a. a.a. a.a.", "aaaa aaaa aaaa aaaa", "aaaa aaAa aaaa aaAa" }, "aaaa aaaa AAAA aaaa", 0.65f, upDown },
            { "Broken chord",       P::Arp, 1, Me | Ho | Ep, Cl | Vo | Or,
              { "a.a. a.a. a.a. a.a.", "a.aa a.aa a.aa a.aa", "aaaa aaaa aaaa aaaa" }, "aaaa aaaa aaaa AAAA", 0.65f, skip },
            { "Psy 16ths",          P::Arp, 1, Da | Dr, Ps | Te,
              { "r.r. r.r. r.r. r.r.", "rrRr rrRr rrRr rrRr", "rRrt rRrf rRrt rRud" }, "uuuu dddd uuuu dddd", 0.5f },
            { "Converging",         P::Arp, 1, Ep | Ho, Or | Up,
              { "a.a. a.a. a.a. a.a.", "a.aa a.aa a.aa a.aa", "aaaa aaaa aaaa aaaa" }, "aaaa aaaa AAAA AAAA", 0.65f, converge },
            { "Dark minor pulse",   P::Arp, 1, Da, Te | Ps | Cl,
              { "r..r ..r. r..r ..r.", "r.tr .tr. r.tr .tr.", "rtfr tfrt rtfr tfRt" }, "rtfR tfrt dddd uuuu", 0.55f },
            { "Random walk",        P::Arp, 1, Da | Dm, Ps | Pr,
              { "r... u... d... u...", "r.u. d.u. u.d. d.u.", "rudu udud rudu uddu" }, "uuuu uuuu dddd dddd", 0.6f },
            // Syncopated, gated, 3-3-2
            { "3-3-2 pluck",        P::Arp, 2, Eu | Ho | Ep, Up | Cl | Pr,
              { "a..a ..a. a..a ..a.", "a..a ..a. a..a ..aa", "a..A ..a. a..A ..aA" }, "a..A ..A. A..A ..AA", 0.7f, up },
            { "Gated 16ths",        P::Arp, 2, Dr | Da | Eu, Te | Cl | Up,
              { "a.a. a.a. a.a. a.a.", "aa.a aa.a aa.a aa.a", "aa.a aa.a aaaa aa.a" }, "aaaa aaaa aaaa aaaa", 0.55f, up },
            { "Syncopated pluck",   P::Arp, 2, Me | Dr | Dm, Pr | Te | Vo,
              { "..a. ..a. ..a. ..a.", "a..a ..a. .a.. a.a.", "a.aa ..a. aa.a .aa." }, "a.aa .aa. aa.a AAAA", 0.65f, up },
            { "Trance gate pluck",  P::Arp, 2, Eu | Dr, Up | Cl,
              { "a.a. a.a. a.a. a.a.", "a.aa .aa. a.aa .aa.", "a.aa .aa. aa.a .aaa" }, "aaaa aaaa aaaa aaaa", 0.6f, up },
            { "Offbeat arp",        P::Arp, 2, Me | Ho | Ep, Vo | Pr | Or,
              { "..a. ..a. ..a. ..a.", "..aa ..aa ..aa ..aa", ".aaa .aaa .aaa .aaa" }, ".aaa .aaa .AAA .AAA", 0.65f, upDown },

            //======================================================================== CHORDS
            // Pads
            { "Full sustain",       P::Chords, 0, allM, allS,
              { "c--- ---- ---- ----", "c--- ---- ---- ----", "c--- ---- ---- ----" }, "c--- ---- ---- C---", 1.0f },
            { "Half-bar swells",    P::Chords, 0, Me | Dm | Ho, Pr | Vo | Or,
              { "c--- ---- ---- ----", "c--- ---- c--- ----", "c--- ---- C--- ----" }, "c--- ---- c--- C---", 1.0f },
            { "Pulsing pad",        P::Chords, 0, Ep | Eu, Up | Cl | Or,
              { "c--- ---- ---- ----", "c--- c--- c--- c---", "c-c- c-c- c-c- c-c-" }, "c-c- c-c- C-C- C-C-", 0.95f },
            { "Breathing pad",      P::Chords, 0, Da | Dm, Pr | Ps | Te,
              { "c--- ---- ---- ----", "c--- --.. c--- --..", "c--. c--. c--. c-.." }, "c--. c--. C--. C-..", 0.95f },
            // Stabs and plucks
            { "Offbeat stabs",      P::Chords, 1, Eu | Dr | Ep, Up | Cl | Te,
              { "..c. ..c. ..c. ..c.", "..c. ..c. ..c. ..cc", "..c. ..C. ..c. ..CC" }, "..c. ..C. ..C. .CCC", 0.7f },
            { "3-3-2 plucks",       P::Chords, 1, Eu | Ho, Up | Pr | Vo,
              { "c... ..c. c... ..c.", "c..c ..c. c..c ..c.", "c..c ..c. c..c ..cC" }, "c..c ..c. C..C ..CC", 0.7f },
            { "Progressive chops",  P::Chords, 1, Me | Dm | Da, Pr | Vo,
              { "c... ...c ..c. ....", "c..c ..c. .c.. c.c.", "c.cc ..c. .cc. c.c." }, "c.cc ..c. .CC. C.C.", 0.7f },
            { "Tech hits",          P::Chords, 1, Da | Dr, Te | Ps,
              { "c... .... c... ....", "c..c .... c..c ....", "c..c ..c. c..c .c.c" }, "c..c ..c. c.cc cccc", 0.5f },
            { "Euphoric 16th pluck", P::Chords, 1, Eu | Ep, Up | Cl,
              { "c.c. c.c. c.c. c.c.", "c.cc c.cc c.cc c.cc", "cccc cccc cccc cccC" }, "cccc cccc CCCC CCCC", 0.55f },
            { "Orchestral stabs",   P::Chords, 1, Ep | Ho, Or | Up,
              { "c... .... c... ....", "c... ..c. c... ..c.", "c.cc ..c. c.cc ..C." }, "c.cc ..C. C.CC .CCC", 0.75f },
            // Gates
            { "Classic gate",       P::Chords, 2, Eu | Ho | Ep, Up | Cl | Vo,
              { "c.c. c.c. c.c. c.c.", "c.cc .cc. c.cc .cc.", "cc.c cc.c c.cc c.cc" }, "cccc cccc cccc cccc", 0.6f },
            { "Rolling gate",       P::Chords, 2, Dr | Da, Te | Cl,
              { "c.c. c.c. c.c. c.c.", "ccc. ccc. ccc. ccc.", "cccc ccc. cccc ccc." }, "cccc cccc cccc cccc", 0.6f },
            { "Stutter gate",       P::Chords, 2, Dr | Eu, Up | Te | Ps,
              { "c..c ..c. c..c ..c.", "c.cc ..cc c.cc ..cc", "cc.c c.cc cc.c c.cc" }, "cccc .ccc cccc .ccc", 0.55f },
            { "Dreamy gate",        P::Chords, 2, Dm | Me, Pr | Vo,
              { "c-.. c-.. c-.. c-..", "c-.c -.c- .c-. c-.c", "c-cc -cc- c-cc -cc-" }, "c-cc c-cc C-CC C-CC", 0.9f },
            { "Dark gate",          P::Chords, 2, Da, Te | Ps | Cl,
              { "c... c... c... c...", "c.c. .c.c c.c. .c.c", "c.cc .c.c c.cc .ccc" }, "cccc .c.c cccc cccc", 0.55f },
            { "Anthem gate",        P::Chords, 2, Ep | Eu, Or | Up | Vo,
              { "c.c. c.c. c.c. c.c.", "c.cc c.cc c.cc c.cc", "cccc c.cc cccc c.cC" }, "cccc CCCC cccc CCCC", 0.6f },

            //======================================================================== LEAD HOOKS (two bars)
            { "Anthem long notes",  P::Lead, 0, Ep | Eu | Ho, Up | Or | Vo,
              { "r--- ---- f--- ----|t--- ---- ---- ----", "r--- --f- --t- ----|f--- --u- d--- ----", "r--- --f- --T- ---u|F--- --u- d--- -t--" },
              "t--- --u- --u- ----|r--- ---- ---- ----", 0.95f },
            { "3-3-2 hook",         P::Lead, 0, Eu | Ho | Dr, Up | Cl | Pr,
              { "r..r ..f. .... ....|t..t ..r. .... ....", "r..r ..f. t..t ..f.|r..r ..f. u..u ..d.", "R..r ..F. t..T ..f.|R..r ..F. u..u ..dd" },
              "f..f ..t. r..r ..u.|t--- ---- ---- ----", 0.85f },
            { "Stepwise climb",     P::Lead, 0, Me | Ho | Dm, Vo | Pr | Or,
              { "r--- ---- u--- ----|u--- ---- ---- ----", "r-.u -.u- u--- d---|t--- ---- d--- ----", "r-uu -.u- u-ud d---|t-ud ---- d-r- ----" },
              "f--- d--- d--- d---|r--- ---- ---- ----", 0.95f },
            { "Driving 16ths",      P::Lead, 0, Dr | Da, Te | Cl | Ps,
              { "r.r. .r.. r.r. .f..|r.r. .r.. r.r. .t..", "r.rr .r.f r.rr .f.t|r.rr .r.f r.rr .t.r", "rRrr .r.f rRrr .f.t|rRrr .r.F rRrr .T.r" },
              "rrrr uuuu rrrr dddd|r.r. r.r. r--- ----", 0.6f },
            { "Call and response",  P::Lead, 0, Eu | Ho | Me, Up | Vo | Cl,
              { "r--- f--- .... ....|.... .... t--- ----", "r..f ..t- ---- ....|.... .... t..r ..d-", "r..f ..T- --u- ....|.... ..u. t..r ..d-" },
              "t..r ..f- ---- ....|f..t ..r- ---- ----", 0.9f },
            { "Dark motif",         P::Lead, 0, Da | Dr, Te | Ps | Cl,
              { "r--- ---- d--- ----|r--- ---- u--- ----", "r..r ..d- r..r ..u-|r..r ..d- d--- ----", "R..r ..d- R..r ..u-|R..r ..d- dd.d ----" },
              "r.dd .r.u r.dd .r.u|r--- ---- ---- ----", 0.8f },
            { "Dreamy float",       P::Lead, 0, Dm | Me, Pr | Vo,
              { "f--- ---- ---- ----|t--- ---- ---- ----", "f--- --u- ---- d---|t--- ---- --d- ----", "F--- --u- --d- d---|t--- --u- --d- ----" },
              "r--- --t- --f- ----|R--- ---- ---- ----", 1.0f },
            { "Euphoric leap",      P::Lead, 0, Eu | Ep, Up | Cl | Or,
              { "r--- R--- ---- ----|f--- ---- ---- ----", "r..R ..f- t..T ..f-|r..R ..F- d--- ----", "r.RR ..f- t.TT ..f-|r.RR ..F- d-d- d---" },
              "F..f ..t- R..r ..f-|R--- ---- ---- ----", 0.85f },
            { "Psy riff",           P::Lead, 0, Da | Dr, Ps | Te,
              { "r.r. r.r. .... ....|r.r. r.d. .... ....", "rrdr udrr rrdr udrr|rrdr udrr rrud dddd", "rrdr udRr rrdr udRr|rrdr udRr rrud dddd" },
              "uuuu uuuu dddd dddd|r.r. r.r. r--- ----", 0.55f },
            { "Orchestral theme",   P::Lead, 0, Ep | Ho | Me, Or | Up,
              { "r--- ---- ---- ----|f--- ---- ---- ----", "r--- --u- u--- ----|f--- --d- t--- ----", "r--- -u-u u--- -u--|F--- -d-d t--- ----" },
              "t--- u--- u--- u---|R--- ---- ---- ----", 1.0f },
            { "Vocal-friendly hook", P::Lead, 0, Ho | Me | Eu, Vo | Pr,
              { "r--- ---- .... ....|t--- ---- .... ....", "r--. ..f. t--- ....|f--. ..t. r--- ....", "r--. .uf. t--- ..u.|f--. .dt. r--- ...." },
              "t--. ..r. f--- ....|r--- ---- ---- ----", 0.9f },
            { "Pedal hook",         P::Lead, 0, Eu | Dr | Ep, Up | Cl | Te,
              { "r.r. r.r. r.r. r.r.|r.r. r.r. r.r. r.r.", "r.r. r.f. r.r. r.t.|r.r. r.f. r.r. R.f.", "R.r. R.F. R.r. R.T.|R.r. R.F. R.r. R.f." },
              "R.R. F.F. T.T. f.f.|r--- ---- ---- ----", 0.7f },
        };
        return lib;
    }

    /** A family's bars as 16-character strings (spaces dropped; short bars padded with rests). */
    inline std::vector<std::string> bars (const char* text)
    {
        std::vector<std::string> out (1);
        for (const char* p = text; p != nullptr && *p != 0; ++p)
        {
            if (*p == ' ') continue;
            if (*p == '|') { out.emplace_back(); continue; }
            out.back().push_back (*p);
        }
        for (auto& b : out) { if (b.size() > 16) b.resize (16); while (b.size() < 16) b.push_back ('.'); }
        return out;
    }

    inline uint32_t hash (uint32_t x)
    {
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return x;
    }

    /** All the families for a part and kind that fit the mood and style best (style counts more:
        it's the rhythm language; the mood colours it). */
    inline std::vector<int> candidates (Part part, int kind, Mood mood, Style style)
    {
        std::vector<int> best;
        int bestScore = -1;
        const auto& lib = library();
        for (int i = 0; i < (int) lib.size(); ++i)
        {
            const auto& f = lib[(size_t) i];
            if (f.part != part || f.kind != kind) continue;
            const auto score = ((f.styles >> (int) style) & 1u) * 3 + ((f.moods >> (int) mood) & 1u) * 2;
            if ((int) score > bestScore) { bestScore = (int) score; best.clear(); }
            if ((int) score == bestScore) best.push_back (i);
        }
        return best;
    }

    /** The song's family for a part and kind: one of the best fits, chosen by the seed (the same
        seed always gives the same song). */
    inline const Family& pick (Part part, int kind, Mood mood, Style style, uint32_t seed)
    {
        auto c = candidates (part, kind, mood, style);
        if (c.empty()) c = candidates (part, 0, mood, style);
        jassert (! c.empty());
        return library()[(size_t) c[hash (seed * 2654435761u + (uint32_t) part * 977u + (uint32_t) kind * 131u) % c.size()]];
    }
}
