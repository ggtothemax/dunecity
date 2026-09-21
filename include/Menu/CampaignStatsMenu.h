/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CAMPAIGNSTATSMENU_H
#define CAMPAIGNSTATSMENU_H

#include "MenuBase.h"

#include <GUI/StaticContainer.h>
#include <GUI/Label.h>
#include <GUI/ProgressBar.h>

#include <string>

class CampaignStatsMenu : public MenuBase {
public:
    explicit CampaignStatsMenu(int level);
    virtual ~CampaignStatsMenu();

    int showMenu() override;

    bool doInput(SDL_Event &event) override;

    void drawSpecificStuff() override;

private:
    void doState(int elapsedTime);

    void calculateScore(int level);

    typedef enum {
        State_HumanSpice,
        State_Between_HumanSpice_and_AISpice,
        State_AISpice,
        State_Between_AISpice_and_HumanUnits,
        State_HumanUnits,
        State_Between_HumanUnits_and_AIUnits,
        State_AIUnits,
        State_Between_AIUnits_and_HumanBuildings,
        State_HumanBuildings,
        State_Between_HumanBuildings_and_AIBuildings,
        State_AIBuildings,
        State_Between_AIBuildings_and_HumanTax,
        State_HumanTax,
        State_Between_HumanTax_and_AITax,
        State_AITax,
        State_Finished
    } CampaignStatsState;


    StaticContainer windowWidget;
    Label scoreLabel;
    Label timeLabel;

    Label yourRankLabel;
    Label rankLabel;

    // spice statistics
    Label       spiceHarvestedByLabel;
    Label       you1Label;
    ProgressBar spiceYouShadowProgressBar;
    ProgressBar spiceYouProgressBar;
    Label       spiceYouLabel;
    Label       enemy1Label;
    ProgressBar spiceEnemyShadowProgressBar;
    ProgressBar spiceEnemyProgressBar;
    Label       spiceEnemyLabel;

    // units statistics
    Label       unitsDestroyedByLabel;
    Label       you2Label;
    ProgressBar unitsYouShadowProgressBar;
    ProgressBar unitsYouProgressBar;
    Label       unitsYouLabel;
    Label       enemy2Label;
    ProgressBar unitsEnemyShadowProgressBar;
    ProgressBar unitsEnemyProgressBar;
    Label       unitsEnemyLabel;

    // buildings statistics
    Label       buildingsDestroyedByLabel;
    Label       you3Label;
    ProgressBar buildingsYouShadowProgressBar;
    ProgressBar buildingsYouProgressBar;
    Label       buildingsYouLabel;
    Label       enemy3Label;
    ProgressBar buildingsEnemyShadowProgressBar;
    ProgressBar buildingsEnemyProgressBar;
    Label       buildingsEnemyLabel;

    // city tax statistics (Dune City only)
    Label       taxCollectedByLabel;
    Label       you4Label;
    ProgressBar taxYouShadowProgressBar;
    ProgressBar taxYouProgressBar;
    Label       taxYouLabel;
    Label       enemy4Label;
    ProgressBar taxEnemyShadowProgressBar;
    ProgressBar taxEnemyProgressBar;
    Label       taxEnemyLabel;

    bool showTaxStatistics = false;   ///< true only for Dune City content with a running city simulation

    int currentStateStartTime = 0;
    CampaignStatsState currentState = State_HumanSpice;

    int unitsDestroyedByHuman = 0;
    int unitsDestroyedByAI = 0;

    int structuresDestroyedByHuman = 0;
    int structuresDestroyedByAI = 0;

    float spiceHarvestedByHuman = 0;
    float spiceHarvestedByAI = 0;

    // Display-only sums; doubles accommodate several houses without overflowing FixPoint.
    double taxCollectedByHuman = 0;
    double taxCollectedByAI = 0;

    int totalTime = 0;
    int totalScore = 0;

    std::string rank;
};

#endif //CAMPAIGNSTATSMENU_H
