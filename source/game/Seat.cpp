/*
 *  Copyright (C) 2011-2015  OpenDungeons Team
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "entities/CreatureDefinition.h"

#include "entities/ResearchEntity.h"
#include "entities/Tile.h"
#include "game/Seat.h"
#include "gamemap/GameMap.h"
#include "game/Research.h"
#include "goals/Goal.h"
#include "network/ODPacket.h"
#include "spawnconditions/SpawnCondition.h"
#include "network/ServerNotification.h"
#include "network/ODServer.h"
#include "rooms/Room.h"
#include "spell/Spell.h"
#include "traps/Trap.h"
#include "utils/ConfigManager.h"
#include "utils/Helper.h"
#include "utils/LogManager.h"
#include "utils/Random.h"

const std::string Seat::PLAYER_TYPE_HUMAN = "Human";
const std::string Seat::PLAYER_TYPE_AI = "AI";
const std::string Seat::PLAYER_TYPE_INACTIVE = "Inactive";
const std::string Seat::PLAYER_TYPE_CHOICE = "Choice";
const std::string Seat::PLAYER_FACTION_CHOICE = "Choice";

Seat::Seat(GameMap* gameMap) :
    mGameMap(gameMap),
    mPlayer(nullptr),
    mTeamId(-1),
    mMana(1000),
    mManaDelta(0),
    mStartingX(0),
    mStartingY(0),
    mGoldMined(0),
    mNumCreaturesControlled(0),
    mDefaultWorkerClass(nullptr),
    mNumClaimedTiles(0),
    mHasGoalsChanged(true),
    mGold(0),
    mId(-1),
    mNbTreasuries(0),
    mIsDebuggingVision(false),
    mResearchPoints(0),
    mCurrentResearch(nullptr),
    mNeedRefreshGuiResearchDone(false),
    mNeedRefreshGuiResearchPending(false)
{
}

void Seat::setMapSize(int x, int y)
{
    if(mPlayer == nullptr)
        return;
    if(!mPlayer->getIsHuman())
        return;

    mTilesVision = std::vector<std::vector<std::pair<bool, bool>>>(x);
    for(int xxx = 0; xxx < x; ++xxx)
    {
        mTilesVision[xxx] = std::vector<std::pair<bool, bool>>(y);
        for(int yyy = 0; yyy < y; ++yyy)
        {
            mTilesVision[xxx][yyy] = std::pair<bool, bool>(false, false);
        }
    }
}

void Seat::setTeamId(int teamId)
{
    OD_ASSERT_TRUE_MSG(std::find(mAvailableTeamIds.begin(), mAvailableTeamIds.end(),
        teamId) != mAvailableTeamIds.end(), "Unknown team id=" + Ogre::StringConverter::toString(teamId)
        + ", for seat id=" + Ogre::StringConverter::toString(getId()));
    mTeamId = teamId;
}

void Seat::addGoal(Goal* g)
{
    mUncompleteGoals.push_back(g);
}

unsigned int Seat::numUncompleteGoals()
{
    unsigned int tempUnsigned = mUncompleteGoals.size();

    return tempUnsigned;
}

Goal* Seat::getUncompleteGoal(unsigned int index)
{
    if (index >= mUncompleteGoals.size())
        return nullptr;

    return mUncompleteGoals[index];
}

void Seat::clearUncompleteGoals()
{
    mUncompleteGoals.clear();
}

void Seat::clearCompletedGoals()
{
    mCompletedGoals.clear();
}

unsigned int Seat::numCompletedGoals()
{
    return mCompletedGoals.size();
}

Goal* Seat::getCompletedGoal(unsigned int index)
{
    if (index >= mCompletedGoals.size())
        return nullptr;

    return mCompletedGoals[index];
}

unsigned int Seat::numFailedGoals()
{
    return mFailedGoals.size();
}

Goal* Seat::getFailedGoal(unsigned int index)
{
    if (index >= mFailedGoals.size())
        return nullptr;

    return mFailedGoals[index];
}

unsigned int Seat::getNumClaimedTiles()
{
    return mNumClaimedTiles;
}

void Seat::setNumClaimedTiles(const unsigned int& num)
{
    mNumClaimedTiles = num;
}

void Seat::incrementNumClaimedTiles()
{
    ++mNumClaimedTiles;
}

unsigned int Seat::checkAllGoals()
{
    // Loop over the goals vector and move any goals that have been met to the completed goals vector.
    std::vector<Goal*> goalsToAdd;
    std::vector<Goal*>::iterator currentGoal = mUncompleteGoals.begin();
    while (currentGoal != mUncompleteGoals.end())
    {
        Goal* goal = *currentGoal;
        // Start by checking if the goal has been met by this seat.
        if (goal->isMet(this))
        {
            mCompletedGoals.push_back(goal);

            // Add any subgoals upon completion to the list of outstanding goals.
            for (unsigned int i = 0; i < goal->numSuccessSubGoals(); ++i)
                goalsToAdd.push_back(goal->getSuccessSubGoal(i));

            currentGoal = mUncompleteGoals.erase(currentGoal);

        }
        else
        {
            // If the goal has not been met, check to see if it cannot be met in the future.
            if (goal->isFailed(this))
            {
                mFailedGoals.push_back(goal);

                // Add any subgoals upon completion to the list of outstanding goals.
                for (unsigned int i = 0; i < goal->numFailureSubGoals(); ++i)
                    goalsToAdd.push_back(goal->getFailureSubGoal(i));

                currentGoal = mUncompleteGoals.erase(currentGoal);
            }
            else
            {
                // The goal has not been met but has also not been definitively failed, continue on to the next goal in the list.
                ++currentGoal;
            }
        }
    }

    for(std::vector<Goal*>::iterator it = goalsToAdd.begin(); it != goalsToAdd.end(); ++it)
    {
        Goal* goal = *it;
        mUncompleteGoals.push_back(goal);
    }

    return numUncompleteGoals();
}

unsigned int Seat::checkAllCompletedGoals()
{
    // Loop over the goals vector and move any goals that have been met to the completed goals vector.
    std::vector<Goal*>::iterator currentGoal = mCompletedGoals.begin();
    while (currentGoal != mCompletedGoals.end())
    {
        // Start by checking if this previously met goal has now been unmet.
        if ((*currentGoal)->isUnmet(this))
        {
            mUncompleteGoals.push_back(*currentGoal);

            currentGoal = mCompletedGoals.erase(currentGoal);

            //Signal that the list of goals has changed.
            goalsHasChanged();
        }
        else
        {
            // Next check to see if this previously met goal has now been failed.
            if ((*currentGoal)->isFailed(this))
            {
                mFailedGoals.push_back(*currentGoal);

                currentGoal = mCompletedGoals.erase(currentGoal);

                //Signal that the list of goals has changed.
                goalsHasChanged();
            }
            else
            {
                ++currentGoal;
            }
        }
    }

    return numCompletedGoals();
}

bool Seat::getHasGoalsChanged()
{
    return mHasGoalsChanged;
}

void Seat::resetGoalsChanged()
{
    mHasGoalsChanged = false;
}

void Seat::goalsHasChanged()
{
    //Not locking here as this is supposed to be called from a function that already locks.
    mHasGoalsChanged = true;
}

bool Seat::isAlliedSeat(Seat *seat)
{
    return getTeamId() == seat->getTeamId();
}

bool Seat::canOwnedCreatureBePickedUpBy(Seat* seat)
{
    // Note : if we want to allow players to pickup allied creatures, we can do that here.
    if(this == seat)
        return true;

    return false;
}

bool Seat::canOwnedTileBeClaimedBy(Seat* seat)
{
    if(getTeamId() != seat->getTeamId())
        return true;

    return false;
}

bool Seat::canOwnedCreatureUseRoomFrom(Seat* seat)
{
    if(this == seat)
        return true;

    return false;
}

bool Seat::canRoomBeDestroyedBy(Seat* seat)
{
    if(this == seat)
        return true;

    return false;
}

bool Seat::canTrapBeDestroyedBy(Seat* seat)
{
    if(this == seat)
        return true;

    return false;
}

void Seat::setPlayer(Player* player)
{
    OD_ASSERT_TRUE_MSG(mPlayer == nullptr, "A player=" + mPlayer->getNick() + " already on seat id="
        + Ogre::StringConverter::toString(getId()));

    mPlayer = player;
    mPlayer->mSeat = this;
}


void Seat::addAlliedSeat(Seat* seat)
{
    mAlliedSeats.push_back(seat);
}


void Seat::initSeat()
{
    if(getPlayer() == nullptr)
        return;

    // Spawn pool initialisation
    ConfigManager& config = ConfigManager::getSingleton();
    const std::vector<std::string>& pool = config.getFactionSpawnPool(mFaction);
    OD_ASSERT_TRUE_MSG(!pool.empty(), "Empty spawn pool for faction=" + mFaction);
    for(const std::string& defName : pool)
    {
        const CreatureDefinition* def = mGameMap->getClassDescription(defName);
        OD_ASSERT_TRUE_MSG(def != nullptr, "defName=" + defName);
        if(def == nullptr)
            continue;

        mSpawnPool.push_back(std::pair<const CreatureDefinition*, bool>(def, false));
    }

    // Get the default worker class
    std::string defaultWorkerClass = config.getFactionWorkerClass(mFaction);
    mDefaultWorkerClass = mGameMap->getClassDescription(defaultWorkerClass);
    OD_ASSERT_TRUE_MSG(mDefaultWorkerClass != nullptr, "No valid default worker class for faction: " + mFaction);

    // We use a temporary vector to allow the corresponding functions to check the vector validity
    // and reject its content if it is not valid
    std::vector<ResearchType> researches = mResearchDone;
    mResearchDone.clear();
    setResearchesDone(researches);
    researches = mResearchPending;
    mResearchPending.clear();
    setResearchTree(researches);
}

const CreatureDefinition* Seat::getNextFighterClassToSpawn()
{
    std::vector<std::pair<const CreatureDefinition*, int32_t> > defSpawnable;
    int32_t nbPointsTotal = 0;
    for(std::pair<const CreatureDefinition*, bool>& def : mSpawnPool)
    {
        // Only check for fighter creatures.
        if (!def.first || def.first->isWorker())
            continue;

        const std::vector<const SpawnCondition*>& conditions = ConfigManager::getSingleton().getCreatureSpawnConditions(def.first);
        int32_t nbPointsConditions = 0;
        for(const SpawnCondition* condition : conditions)
        {
            int32_t nbPointsCondition = 0;
            if(!condition->computePointsForSeat(mGameMap, this, nbPointsCondition))
            {
                nbPointsConditions = -1;
                break;
            }
            nbPointsConditions += nbPointsCondition;
        }

        // Check if the creature can spawn. nbPointsConditions < 0 can happen if a condition is not met or if there are too many
        // negative points. In both cases, we don't want the creature to spawn
        if(nbPointsConditions < 0)
            continue;

        // Check if it is the first time this conditions have been fullfilled. If yes, we force this creature to spawn
        if(!def.second && !conditions.empty())
        {
            def.second = true;
            return def.first;
        }
        nbPointsConditions += ConfigManager::getSingleton().getBaseSpawnPoint();
        defSpawnable.push_back(std::pair<const CreatureDefinition*, int32_t>(def.first, nbPointsConditions));
        nbPointsTotal += nbPointsConditions;
    }

    if(defSpawnable.empty())
        return nullptr;

    // We choose randomly a creature to spawn according to their points
    int32_t cpt = Random::Int(0, nbPointsTotal - 1);
    for(std::pair<const CreatureDefinition*, int32_t>& def : defSpawnable)
    {
        if(cpt < def.second)
            return def.first;

        cpt -= def.second;
    }

    // It is not normal to come here
    OD_ASSERT_TRUE_MSG(false, "seatId=" + Ogre::StringConverter::toString(getId()));
    return nullptr;
}

void Seat::clearTilesWithVision()
{
    if(mPlayer == nullptr)
        return;
    if(!mPlayer->getIsHuman())
        return;

    for(std::vector<std::pair<bool, bool>>& vec : mTilesVision)
    {
        for(std::pair<bool, bool>& p : vec)
        {
            p.first = p.second;
            p.second = false;
        }
    }
}

void Seat::notifyVisionOnTile(Tile* tile)
{
    if(mPlayer == nullptr)
        return;
    if(!mPlayer->getIsHuman())
        return;

    OD_ASSERT_TRUE_MSG(tile->getX() < static_cast<int>(mTilesVision.size()), "Tile=" + Tile::displayAsString(tile));
    OD_ASSERT_TRUE_MSG(tile->getY() < static_cast<int>(mTilesVision[tile->getX()].size()), "Tile=" + Tile::displayAsString(tile));
    std::pair<bool, bool>& p = mTilesVision[tile->getX()][tile->getY()];
    p.second = true;
}

bool Seat::hasVisionOnTile(Tile* tile)
{
    if(!mGameMap->isServerGameMap())
    {
        // On client side, we check only for the local player
        if(this != mGameMap->getLocalPlayer()->getSeat())
            return false;

        return tile->getLocalPlayerHasVision();
    }

    // AI players have vision on every tile
    if(mPlayer == nullptr)
        return true;
    if(!mPlayer->getIsHuman())
        return true;

    OD_ASSERT_TRUE_MSG(tile->getX() < static_cast<int>(mTilesVision.size()), "Tile=" + Tile::displayAsString(tile));
    OD_ASSERT_TRUE_MSG(tile->getY() < static_cast<int>(mTilesVision[tile->getX()].size()), "Tile=" + Tile::displayAsString(tile));
    std::pair<bool, bool>& p = mTilesVision[tile->getX()][tile->getY()];

    return p.second;
}

void Seat::notifyChangedVisibleTiles()
{
    if(mPlayer == nullptr)
        return;
    if(!mPlayer->getIsHuman())
        return;

    std::vector<Tile*> tilesToNotify;
    int xMax = static_cast<int>(mTilesVision.size());
    for(int xxx = 0; xxx < xMax; ++xxx)
    {
        int yMax = static_cast<int>(mTilesVision[xxx].size());
        for(int yyy = 0; yyy < yMax; ++yyy)
        {
            if(!mTilesVision[xxx][yyy].second)
                continue;

            Tile* tile = mGameMap->getTile(xxx, yyy);
            if(!tile->hasChangedForSeat(this))
                continue;

            tilesToNotify.push_back(tile);
            tile->changeNotifiedForSeat(this);
        }
    }

    if(tilesToNotify.empty())
        return;

    uint32_t nbTiles = tilesToNotify.size();
    ServerNotification *serverNotification = new ServerNotification(
        ServerNotificationType::refreshTiles, getPlayer());
    serverNotification->mPacket << nbTiles;
    for(Tile* tile : tilesToNotify)
    {
        mGameMap->tileToPacket(serverNotification->mPacket, tile);
        tile->exportTileToPacket(serverNotification->mPacket, this);
    }
    ODServer::getSingleton().queueServerNotification(serverNotification);
}

void Seat::stopVisualDebugEntities()
{
    if(mGameMap->isServerGameMap())
        return;

    mIsDebuggingVision = false;

    for (Tile* tile : mVisualDebugEntityTiles)
    {
        if (tile == nullptr)
            continue;

        RenderManager::getSingleton().rrDestroySeatVisionVisualDebug(getId(), tile);
    }
    mVisualDebugEntityTiles.clear();

}

void Seat::refreshVisualDebugEntities(const std::vector<Tile*>& tiles)
{
    if(mGameMap->isServerGameMap())
        return;

    mIsDebuggingVision = true;

    for (Tile* tile : tiles)
    {
        // We check if the visual debug is already on this tile
        if(std::find(mVisualDebugEntityTiles.begin(), mVisualDebugEntityTiles.end(), tile) != mVisualDebugEntityTiles.end())
            continue;

        RenderManager::getSingleton().rrCreateSeatVisionVisualDebug(getId(), tile);

        mVisualDebugEntityTiles.push_back(tile);
    }

    // now, we check if visual debug should be removed from a tile
    for (std::vector<Tile*>::iterator it = mVisualDebugEntityTiles.begin(); it != mVisualDebugEntityTiles.end();)
    {
        Tile* tile = *it;
        if(std::find(tiles.begin(), tiles.end(), tile) != tiles.end())
        {
            ++it;
            continue;
        }

        it = mVisualDebugEntityTiles.erase(it);

        RenderManager::getSingleton().rrDestroySeatVisionVisualDebug(getId(), tile);
    }
}

void Seat::displaySeatVisualDebug(bool enable)
{
    if(!mGameMap->isServerGameMap())
        return;

    // Visual debugging do not work for AI players (otherwise, we would have to use
    // mTilesVision for them which would be memory consuming)
    if(mPlayer == nullptr)
        return;
    if(!mPlayer->getIsHuman())
        return;

    mIsDebuggingVision = enable;
    int seatId = getId();
    if(enable)
    {
        std::vector<Tile*> tiles;
        int xMax = static_cast<int>(mTilesVision.size());
        for(int xxx = 0; xxx < xMax; ++xxx)
        {
            int yMax = static_cast<int>(mTilesVision[xxx].size());
            for(int yyy = 0; yyy < yMax; ++yyy)
            {
                if(!mTilesVision[xxx][yyy].second)
                    continue;

                Tile* tile = mGameMap->getTile(xxx, yyy);
                tiles.push_back(tile);
            }
        }
        uint32_t nbTiles = tiles.size();
        ServerNotification *serverNotification = new ServerNotification(
            ServerNotificationType::refreshSeatVisDebug, nullptr);
        serverNotification->mPacket << seatId;
        serverNotification->mPacket << true;
        serverNotification->mPacket << nbTiles;
        for(Tile* tile : tiles)
        {
            mGameMap->tileToPacket(serverNotification->mPacket, tile);
        }
        ODServer::getSingleton().queueServerNotification(serverNotification);
    }
    else
    {
        ServerNotification *serverNotification = new ServerNotification(
            ServerNotificationType::refreshSeatVisDebug, nullptr);
        serverNotification->mPacket << seatId;
        serverNotification->mPacket << false;
        ODServer::getSingleton().queueServerNotification(serverNotification);
    }
}

void Seat::sendVisibleTiles()
{
    if(!mGameMap->isServerGameMap())
        return;

    if(getPlayer() == nullptr)
        return;

    if(!getPlayer()->getIsHuman())
        return;

    uint32_t nbTiles;
    ServerNotification *serverNotification = new ServerNotification(
        ServerNotificationType::refreshVisibleTiles, getPlayer());
    std::vector<Tile*> tilesVisionGained;
    std::vector<Tile*> tilesVisionLost;
    // Tiles we gained vision
    int xMax = static_cast<int>(mTilesVision.size());
    for(int xxx = 0; xxx < xMax; ++xxx)
    {
        int yMax = static_cast<int>(mTilesVision[xxx].size());
        for(int yyy = 0; yyy < yMax; ++yyy)
        {
            if(mTilesVision[xxx][yyy].second == mTilesVision[xxx][yyy].first)
                continue;

            Tile* tile = mGameMap->getTile(xxx, yyy);
            if(mTilesVision[xxx][yyy].second)
            {
                // Vision gained
                tilesVisionGained.push_back(tile);
            }
            else
            {
                // Vision lost
                tilesVisionLost.push_back(tile);
            }
        }
    }

    // Notify tiles we gained vision
    nbTiles = tilesVisionGained.size();
    serverNotification->mPacket << nbTiles;
    for(Tile* tile : tilesVisionGained)
    {
        mGameMap->tileToPacket(serverNotification->mPacket, tile);
    }

    // Notify tiles we lost vision
    nbTiles = tilesVisionLost.size();
    serverNotification->mPacket << nbTiles;
    for(Tile* tile : tilesVisionLost)
    {
        mGameMap->tileToPacket(serverNotification->mPacket, tile);
    }
    ODServer::getSingleton().queueServerNotification(serverNotification);
}

void Seat::computeSeatBeforeSendingToClient()
{
    if(mPlayer != nullptr)
    {
        mNbTreasuries = mGameMap->numRoomsByTypeAndSeat(RoomType::treasury, this);
    }
}

ODPacket& operator<<(ODPacket& os, Seat *s)
{
    os << s->mId << s->mTeamId << s->mPlayerType << s->mFaction << s->mStartingX
       << s->mStartingY;
    os << s->mColorId;
    os << s->mGold << s->mMana << s->mManaDelta << s->mNumClaimedTiles;
    os << s->mHasGoalsChanged;
    os << s->mNbTreasuries;
    uint32_t nb = s->mAvailableTeamIds.size();
    os << nb;
    for(int teamId : s->mAvailableTeamIds)
        os << teamId;

    return os;
}

ODPacket& operator>>(ODPacket& is, Seat *s)
{
    is >> s->mId >> s->mTeamId >> s->mPlayerType;
    is >> s->mFaction >> s->mStartingX >> s->mStartingY;
    is >> s->mColorId;
    is >> s->mGold >> s->mMana >> s->mManaDelta >> s->mNumClaimedTiles;
    is >> s->mHasGoalsChanged;
    is >> s->mNbTreasuries;
    s->mColorValue = ConfigManager::getSingleton().getColorFromId(s->mColorId);
    uint32_t nb;
    is >> nb;
    while(nb > 0)
    {
        --nb;
        int teamId;
        is >> teamId;
        s->mAvailableTeamIds.push_back(teamId);
    }

    return is;
}

const std::string Seat::getFactionFromLine(const std::string& line)
{
    const uint32_t indexFactionInLine = 3;
    std::vector<std::string> elems = Helper::split(line, '\t');
    OD_ASSERT_TRUE_MSG(elems.size() > indexFactionInLine, "line=" + line);
    if(elems.size() > indexFactionInLine)
        return elems[indexFactionInLine];

    return std::string();
}

Seat* Seat::getRogueSeat(GameMap* gameMap)
{
    Seat* seat = new Seat(gameMap);
    seat->mId = 0;
    seat->mTeamId = 0;
    seat->mAvailableTeamIds.push_back(0);
    seat->mPlayerType = PLAYER_TYPE_INACTIVE;
    seat->mStartingX = 0;
    seat->mStartingY = 0;
    seat->mGold = 0;
    seat->mGoldMined = 0;
    seat->mMana = 0;
    return seat;
}

void Seat::refreshFromSeat(Seat* s)
{
    // We only refresh data that changes over time (gold, mana, ...)
    mGold = s->mGold;
    mMana = s->mMana;
    mManaDelta = s->mManaDelta;
    mNumClaimedTiles = s->mNumClaimedTiles;
    mHasGoalsChanged = s->mHasGoalsChanged;
    mNbTreasuries = s->mNbTreasuries;
}

bool Seat::takeMana(double mana)
{
    if(mana > mMana)
        return false;

    mMana -= mana;
    return true;
}

bool Seat::sortForMapSave(Seat* s1, Seat* s2)
{
    return s1->mId < s2->mId;
}

bool Seat::importSeatFromStream(std::istream& is)
{
    std::string str;
    OD_ASSERT_TRUE(is >> str);
    if(str != "seatId")
    {
        LogManager::getSingleton().logMessage("WARNING: expected seatId and read " + str);
        return false;
    }
    OD_ASSERT_TRUE(is >> mId);
    if(mId == 0)
    {
        LogManager::getSingleton().logMessage("WARNING: Forbidden seatId used");
        return false;
    }

    OD_ASSERT_TRUE(is >> str);
    if(str != "teamId")
    {
        LogManager::getSingleton().logMessage("WARNING: expected teamId and read " + str);
        return false;
    }
    OD_ASSERT_TRUE(is >> str);

    std::vector<std::string> teamIds = Helper::split(str, '/');
    for(const std::string& strTeamId : teamIds)
    {
        int teamId = Helper::toInt(strTeamId);
        if(teamId == 0)
        {
            LogManager::getSingleton().logMessage("WARNING: forbidden teamId in seat id=" + Helper::toString(mId));
            continue;
        }

        mAvailableTeamIds.push_back(teamId);
    }

    OD_ASSERT_TRUE(is >> str);
    if(str != "player")
    {
        LogManager::getSingleton().logMessage("WARNING: expected player and read " + str);
        return false;
    }
    OD_ASSERT_TRUE(is >> mPlayerType);

    OD_ASSERT_TRUE(is >> str);
    if(str != "faction")
    {
        LogManager::getSingleton().logMessage("WARNING: expected faction and read " + str);
        return false;
    }
    OD_ASSERT_TRUE(is >> mFaction);

    OD_ASSERT_TRUE(is >> str);
    if(str != "startingX")
    {
        LogManager::getSingleton().logMessage("WARNING: expected startingX and read " + str);
        return false;
    }
    OD_ASSERT_TRUE(is >> mStartingX);

    OD_ASSERT_TRUE(is >> str);
    if(str != "startingY")
    {
        LogManager::getSingleton().logMessage("WARNING: expected startingY and read " + str);
        return false;
    }
    OD_ASSERT_TRUE(is >> mStartingY);

    OD_ASSERT_TRUE(is >> str);
    if(str != "colorId")
    {
        LogManager::getSingleton().logMessage("WARNING: expected colorId and read " + str);
        return false;
    }
    OD_ASSERT_TRUE(is >> mColorId);

    OD_ASSERT_TRUE(is >> str);
    if(str != "gold")
    {
        LogManager::getSingleton().logMessage("WARNING: expected gold and read " + str);
        return false;
    }
    OD_ASSERT_TRUE(is >> mGold);

    OD_ASSERT_TRUE(is >> str);
    if(str != "goldMined")
    {
        LogManager::getSingleton().logMessage("WARNING: expected goldMined and read " + str);
        return false;
    }
    OD_ASSERT_TRUE(is >> mGoldMined);

    OD_ASSERT_TRUE(is >> str);
    if(str != "mana")
    {
        LogManager::getSingleton().logMessage("WARNING: expected mana and read " + str);
        return false;
    }
    OD_ASSERT_TRUE(is >> mMana);

    mColorValue = ConfigManager::getSingleton().getColorFromId(mColorId);

    uint32_t nbResearch = static_cast<uint32_t>(ResearchType::countResearch);
    OD_ASSERT_TRUE(is >> str);
    if(str != "[ResearchDone]")
    {
        LogManager::getSingleton().logMessage("WARNING: expected [ResearchDone] and read " + str);
        return false;
    }
    while(true)
    {
        OD_ASSERT_TRUE(is >> str);
        if(str == "[/ResearchDone]")
            break;

        for(uint32_t i = 0; i < nbResearch; ++i)
        {
            ResearchType type = static_cast<ResearchType>(i);
            if(type == ResearchType::nullResearchType)
                continue;
            if(str.compare(Research::researchTypeToString(type)) != 0)
                continue;

            if(std::find(mResearchDone.begin(), mResearchDone.end(), type) != mResearchDone.end())
                break;

            // We found a valid research
            mResearchDone.push_back(type);

            break;
        }
    }

    OD_ASSERT_TRUE(is >> str);
    if(str != "[ResearchNotAllowed]")
    {
        LogManager::getSingleton().logMessage("WARNING: expected [ResearchNotAllowed] and read " + str);
        return false;
    }

    while(true)
    {
        OD_ASSERT_TRUE(is >> str);
        if(str == "[/ResearchNotAllowed]")
            break;

        for(uint32_t i = 0; i < nbResearch; ++i)
        {
            ResearchType type = static_cast<ResearchType>(i);
            if(type == ResearchType::nullResearchType)
                continue;
            if(str.compare(Research::researchTypeToString(type)) != 0)
                continue;

            // We do not allow a research to be done and not allowed
            if(std::find(mResearchDone.begin(), mResearchDone.end(), type) != mResearchDone.end())
                break;
            if(std::find(mResearchNotAllowed.begin(), mResearchNotAllowed.end(), type) != mResearchNotAllowed.end())
                break;

            // We found a valid research
            mResearchNotAllowed.push_back(type);

            break;
        }
    }

    OD_ASSERT_TRUE(is >> str);
    if(str != "[ResearchPending]")
    {
        LogManager::getSingleton().logMessage("WARNING: expected [ResearchPending] and read " + str);
        return false;
    }

    while(true)
    {
        OD_ASSERT_TRUE(is >> str);
        if(str == "[/ResearchPending]")
            break;

        for(uint32_t i = 0; i < nbResearch; ++i)
        {
            ResearchType type = static_cast<ResearchType>(i);
            if(type == ResearchType::nullResearchType)
                continue;
            if(str.compare(Research::researchTypeToString(type)) != 0)
                continue;

            // We do not allow researches already done or not allowed
            if(std::find(mResearchDone.begin(), mResearchDone.end(), type) != mResearchDone.end())
                break;
            if(std::find(mResearchNotAllowed.begin(), mResearchNotAllowed.end(), type) != mResearchNotAllowed.end())
                break;
            if(std::find(mResearchPending.begin(), mResearchPending.end(), type) != mResearchPending.end())
                break;

            // We found a valid research
            mResearchPending.push_back(type);
        }
    }
    return true;
}

bool Seat::exportSeatToStream(std::ostream& os) const
{
    os << "seatId\t";
    os << mId;
    os << std::endl;
    // If the team id is set, we save it. Otherwise, we save all the available team ids
    // That way, save map will work in both editor and in game.
    os << "teamId\t";
    if(mTeamId != -1)
    {
        os << mTeamId;
    }
    else
    {
        int cpt = 0;
        for(int teamId : mAvailableTeamIds)
        {
            if(cpt > 0)
                os << "/";

            os << teamId;
            ++cpt;
        }
    }
    os << std::endl;

    // On editor, we write the original player type. If we are saving a game, we keep the assigned type
    if((mGameMap->isInEditorMode()) ||
       (getPlayer() == nullptr))
    {
        os << "player\t";
        os << mPlayerType;
        os << std::endl;
    }
    else
    {
        os << "player\t";
        if(getPlayer()->getIsHuman())
            os << PLAYER_TYPE_HUMAN;
        else
            os << PLAYER_TYPE_AI;

        os << std::endl;
    }

    os << "faction\t";
    os << mFaction;
    os << std::endl;

    os << "startingX\t";
    os << mStartingX;
    os << std::endl;

    os << "startingY\t";
    os << mStartingY;
    os << std::endl;

    os << "colorId\t";
    os << mColorId;
    os << std::endl;

    os << "gold\t";
    os << mGold;
    os << std::endl;

    os << "goldMined\t";
    os << mGoldMined;
    os << std::endl;

    os << "mana\t";
    os << mMana;
    os << std::endl;

    os << "[ResearchDone]" << std::endl;
    for(ResearchType type : mResearchDone)
    {
        os << Research::researchTypeToString(type) << std::endl;
    }
    os << "[/ResearchDone]" << std::endl;

    os << "[ResearchNotAllowed]" << std::endl;
    for(ResearchType type : mResearchNotAllowed)
    {
        os << Research::researchTypeToString(type) << std::endl;
    }
    os << "[/ResearchNotAllowed]" << std::endl;

    os << "[ResearchPending]" << std::endl;
    for(ResearchType type : mResearchPending)
    {
        os << Research::researchTypeToString(type) << std::endl;
    }
    os << "[/ResearchPending]" << std::endl;

    return true;
}

bool Seat::isSpellAvailable(SpellType type) const
{
    switch(type)
    {
        case SpellType::summonWorker:
            return isResearchDone(ResearchType::spellSummonWorker);
        case SpellType::callToWar:
            return isResearchDone(ResearchType::spellCallToWar);
        default:
            OD_ASSERT_TRUE_MSG(false, "Unknown enum value : " + Helper::toString(
                static_cast<int>(type)) + " for seatId " + Helper::toString(getId()));
            return false;
    }
    return false;
}

bool Seat::isRoomAvailable(RoomType type) const
{
    switch(type)
    {
        case RoomType::treasury:
            return isResearchDone(ResearchType::roomTreasury);
        case RoomType::dormitory:
            return isResearchDone(ResearchType::roomDormitory);
        case RoomType::hatchery:
            return isResearchDone(ResearchType::roomHatchery);
        case RoomType::trainingHall:
            return isResearchDone(ResearchType::roomTrainingHall);
        case RoomType::library:
            return isResearchDone(ResearchType::roomLibrary);
        case RoomType::forge:
            return isResearchDone(ResearchType::roomForge);
        case RoomType::crypt:
            return isResearchDone(ResearchType::roomCrypt);
        default:
            OD_ASSERT_TRUE_MSG(false, "Unknown enum value : " + Helper::toString(
                static_cast<int>(type)) + " for seatId " + Helper::toString(getId()));
            return false;
    }
    return false;
}

bool Seat::isTrapAvailable(TrapType type) const
{
    switch(type)
    {
        case TrapType::boulder:
            return isResearchDone(ResearchType::trapBoulder);
        case TrapType::cannon:
            return isResearchDone(ResearchType::trapCannon);
        case TrapType::spike:
            return isResearchDone(ResearchType::trapSpike);
        default:
            OD_ASSERT_TRUE_MSG(false, "Unknown enum value : " + Helper::toString(
                static_cast<int>(type)) + " for seatId " + Helper::toString(getId()));
            return false;
    }
    return false;
}

bool Seat::addResearch(ResearchType type)
{
    if(std::find(mResearchDone.begin(), mResearchDone.end(), type) != mResearchDone.end())
        return false;

    std::vector<ResearchType> researchDone = mResearchDone;
    researchDone.push_back(type);
    setResearchesDone(researchDone);

    return true;
}

bool Seat::isResearchDone(ResearchType type) const
{
    for(ResearchType researchDone : mResearchDone)
    {
        if(researchDone != type)
            continue;

        return true;
    }

    return false;
}

const Research* Seat::addResearchPoints(int32_t points)
{
    if(mCurrentResearch == nullptr)
        return nullptr;

    mResearchPoints += points;
    if(mResearchPoints < mCurrentResearch->getNeededResearchPoints())
        return nullptr;

    const Research* ret = mCurrentResearch;
    mResearchPoints -= mCurrentResearch->getNeededResearchPoints();

    // The current research is complete. The library that completed it will release
    // a ResearchEntity. Once it will reach its destination, the research will be
    // added to the done list

    setNextResearch(mCurrentResearch->getType());
    return ret;
}

void Seat::setNextResearch(ResearchType researchedType)
{
    mCurrentResearch = nullptr;
    if(!mResearchPending.empty())
    {
        // We search for the first pending research we don't own a corresponding ResearchEntity
        const std::vector<RenderedMovableEntity*>& renderables = mGameMap->getRenderedMovableEntities();
        ResearchType researchType = ResearchType::nullResearchType;
        for(ResearchType pending : mResearchPending)
        {
            if(pending == researchedType)
                continue;

            researchType = pending;
            for(RenderedMovableEntity* renderable : renderables)
            {
                if(renderable->getObjectType() != GameEntityType::researchEntity)
                    continue;

                if(renderable->getSeat() != this)
                    continue;

                ResearchEntity* researchEntity = static_cast<ResearchEntity*>(renderable);
                if(researchEntity->getResearchType() != pending)
                    continue;

                // We found a ResearchEntity of the same Research. We should work on
                // something else
                researchType = ResearchType::nullResearchType;
                break;
            }

            if(researchType != ResearchType::nullResearchType)
                break;
        }

        if(researchType == ResearchType::nullResearchType)
            return;

        // We have found a fitting research. We retrieve the corresponding Research
        // object and start working on that
        const std::vector<const Research*>& researches = ConfigManager::getSingleton().getResearches();
        for(const Research* research : researches)
        {
            if(research->getType() != researchType)
                continue;

            mCurrentResearch = research;
            break;
        }
    }
}

void Seat::setResearchesDone(const std::vector<ResearchType>& researches)
{
    mResearchDone = researches;
    // We remove the researches done from the pending researches (if it was there,
    // which may not be true if the research list changed after creating the
    // researchEntity for example)
    for(ResearchType type : researches)
    {
        auto research = std::find(mResearchPending.begin(), mResearchPending.end(), type);
        if(research == mResearchPending.end())
            continue;

        mResearchPending.erase(research);
    }

    if(mGameMap->isServerGameMap())
    {
        if((getPlayer() != nullptr) && getPlayer()->getIsHuman())
        {
            // We notify the client
            ServerNotification *serverNotification = new ServerNotification(
                ServerNotificationType::researchesDone, getPlayer());

            uint32_t nbItems = mResearchDone.size();
            serverNotification->mPacket << nbItems;
            for(ResearchType research : mResearchDone)
                serverNotification->mPacket << research;

            ODServer::getSingleton().queueServerNotification(serverNotification);
        }
    }
    else
    {
        // We notify the mode that the available researches changed. This way, it will
        // be able to update the UI as needed
        mNeedRefreshGuiResearchDone = true;
    }
}

void Seat::setResearchTree(const std::vector<ResearchType>& researches)
{
    if(mGameMap->isServerGameMap())
    {
        // We check if all the researches in the vector are allowed. If not, we don't update the list
        const std::vector<const Research*>& researchList = ConfigManager::getSingleton().getResearches();
        std::vector<ResearchType> researchesDoneInTree = mResearchDone;
        for(ResearchType researchType : researches)
        {
            // We check if the research is allowed
            if(std::find(mResearchNotAllowed.begin(), mResearchNotAllowed.end(), researchType) != mResearchNotAllowed.end())
            {
                // Invalid research. This might be allowed in the gui to enter invalid
                // values. In this case, we should remove the assert
                OD_ASSERT_TRUE_MSG(false, "Unallowed research: " + Research::researchTypeToString(researchType));
                return;
            }
            const Research* research = nullptr;
            for(const Research* researchTmp : researchList)
            {
                if(researchTmp->getType() != researchType)
                    continue;

                research = researchTmp;
                break;
            }

            if(research == nullptr)
            {
                // We found an unknow research
                OD_ASSERT_TRUE_MSG(false, "Unknow research: " + Research::researchTypeToString(researchType));
                return;
            }

            if(!research->canBeResearched(researchesDoneInTree))
            {
                // Invalid research. This might be allowed in the gui to enter invalid
                // values. In this case, we should remove the assert
                OD_ASSERT_TRUE_MSG(false, "Unallowed research: " + Research::researchTypeToString(researchType));
                return;
            }

            // This research is valid. We add it in the list and we check if the next one also is
            researchesDoneInTree.push_back(researchType);
        }

        mResearchPending = researches;
        if((getPlayer() != nullptr) && getPlayer()->getIsHuman())
        {
            // We notify the client
            ServerNotification *serverNotification = new ServerNotification(
                ServerNotificationType::researchTree, getPlayer());

            uint32_t nbItems = mResearchPending.size();
            serverNotification->mPacket << nbItems;
            for(ResearchType research : mResearchPending)
                serverNotification->mPacket << research;

            ODServer::getSingleton().queueServerNotification(serverNotification);
        }

        // We start working on the research tree
        setNextResearch(ResearchType::nullResearchType);
    }
    else
    {
        // On client side, no need to check if the research tree is allowed
        mResearchPending = researches;
        mNeedRefreshGuiResearchPending = true;
    }
}
