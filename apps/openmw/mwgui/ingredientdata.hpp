#ifndef MWGUI_INGREDIENTDATA_H
#define MWGUI_INGREDIENTDATA_H

/*
    majere addition (ingredient finder): the static half of the data. The table itself lives in the
    GENERATED ingredientdata.cpp (tools/gen_ingredient_table.py, from the vanilla game's restocking sellers).
*/

#include <string>
#include <vector>

namespace MWGui
{
    struct IngredientSeller
    {
        std::string npc;
        std::string cell;   // full interior cell name, e.g. "Balmora, Temple"
    };

    struct IngredientDef
    {
        std::string name;                       // display name
        int group;                              // 0 green, 1 red, 2 white
        std::vector<std::string> ids;           // record ids (lowercase); Belladonna has two
        std::vector<IngredientSeller> sellers;  // restocking sellers (vanilla baseline)
    };

    const std::vector<IngredientDef>& ingredientTable();
}

#endif
