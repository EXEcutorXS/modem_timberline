#include "operator_names.h"
#include <string.h>

struct OperatorEntry { const char* code; const char* name; };

/* Popular carriers — Russia, the US, and the major European markets.
   MCC+MNC assignments shift over time (mergers, re-farming); extend as needed.
   Country suffix is added only where a brand name repeats across countries. */
static const OperatorEntry kOperators[] = {
    /* Russia (MCC 250) */
    {"25001", "MTS"},
    {"25002", "MegaFon"},
    {"25020", "Tele2 RU"},
    {"25099", "Beeline"},

    /* USA (MCC 310/311) */
    {"310410", "AT&T"},
    {"310150", "AT&T"},
    {"310380", "AT&T"},
    {"310260", "T-Mobile US"},
    {"310200", "T-Mobile US"},
    {"311480", "Verizon"},
    {"310120", "Sprint"},

    /* Germany (MCC 262) */
    {"26201", "Telekom.de"},
    {"26202", "Vodafone DE"},
    {"26203", "o2 DE"},
    {"26207", "o2 DE"},

    /* United Kingdom (MCC 234/235) */
    {"23410", "O2 UK"},
    {"23415", "Vodafone UK"},
    {"23420", "Three UK"},
    {"23430", "EE"},
    {"23433", "EE"},

    /* France (MCC 208) */
    {"20801", "Orange FR"},
    {"20810", "SFR"},
    {"20820", "Bouygues"},
    {"20888", "Free FR"},

    /* Italy (MCC 222) */
    {"22201", "TIM"},
    {"22210", "Vodafone IT"},
    {"22288", "WindTre"},
    {"22299", "WindTre"},

    /* Spain (MCC 214) */
    {"21401", "Vodafone ES"},
    {"21403", "Orange ES"},
    {"21407", "Movistar"},

    /* Poland (MCC 260) */
    {"26001", "Plus"},
    {"26002", "T-Mobile PL"},
    {"26003", "Orange PL"},
    {"26006", "Play"},

    /* Netherlands (MCC 204) */
    {"20404", "Vodafone NL"},
    {"20408", "KPN"},
    {"20416", "T-Mobile NL"},

    /* Switzerland (MCC 228) */
    {"22801", "Swisscom"},
    {"22802", "Sunrise"},
    {"22803", "Salt"},

    /* Austria (MCC 232) */
    {"23201", "A1"},
    {"23205", "Magenta"},
    {"23210", "Drei"},

    /* Sweden (MCC 240) */
    {"24001", "Telia SE"},
    {"24007", "Tele2 SE"},
    {"24008", "Telenor SE"},

    /* Belgium (MCC 206) */
    {"20601", "Proximus"},
    {"20610", "Orange BE"},
    {"20620", "Base"},

    /* Portugal (MCC 268) */
    {"26801", "Vodafone PT"},
    {"26803", "NOS"},
    {"26806", "MEO"},

    /* Czech Republic (MCC 230) */
    {"23001", "T-Mobile CZ"},
    {"23002", "O2 CZ"},
    {"23003", "Vodafone CZ"},

    /* Turkey (MCC 286) */
    {"28601", "Turkcell"},
    {"28602", "Vodafone TR"},
    {"28603", "TurkTelekom"},
};

const char* findOperatorName(const char* mccMnc)
{
    if (!mccMnc || !mccMnc[0]) return 0;

    for (unsigned i = 0; i < sizeof(kOperators) / sizeof(kOperators[0]); i++)
        if (!strcmp(kOperators[i].code, mccMnc))
            return kOperators[i].name;

    return 0;
}
