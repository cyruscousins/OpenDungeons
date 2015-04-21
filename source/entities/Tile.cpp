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

#include "entities/Tile.h"

#include "game/Player.h"
#include "game/Seat.h"

#include "gamemap/GameMap.h"

#include "network/ODPacket.h"

#include "utils/Helper.h"
#include "utils/LogManager.h"

#include <cstddef>
#include <bitset>
#include <istream>
#include <ostream>

#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
#define snprintf_is_banned_in_OD_code _snprintf
#endif

const std::string TILE_PREFIX = "Tile_";
const std::string TILE_SCANF = TILE_PREFIX + "%i_%i";

Tile::Tile(GameMap* gameMap, int x, int y, TileType type, double fullness) :
    EntityBase({}, {}),
    mX                  (x),
    mY                  (y),
    mType               (type),
    mTileVisual         (TileVisual::nullTileVisual),
    mSelected           (false),
    mFullness           (fullness),
    mRefundPriceRoom    (0),
    mRefundPriceTrap    (0),
    mCoveringBuilding   (nullptr),
    mFloodFillColor     (std::vector<int>(static_cast<int>(FloodFillType::nbValues), -1)),
    mClaimedPercentage  (0.0),
    mScale              (Ogre::Vector3::ZERO),
    mIsBuilding         (false),
    mLocalPlayerHasVision   (false),
    mGameMap(gameMap)
{
    setSeat(nullptr);
    computeTileVisual();
}

bool Tile::isDiggable(const Seat* seat) const
{
    // Handle non claimed
    switch(mTileVisual)
    {
        case TileVisual::claimedGround:
        case TileVisual::dirtGround:
        case TileVisual::goldGround:
        case TileVisual::lavaGround:
        case TileVisual::waterGround:
        case TileVisual::rockGround:
        case TileVisual::rockFull:
            return false;
        case TileVisual::goldFull:
        case TileVisual::dirtFull:
            return true;
        default:
            break;
    }

    // Should be claimed tile
    OD_ASSERT_TRUE_MSG(mTileVisual == TileVisual::claimedFull, "mTileVisual=" + tileVisualToString(mTileVisual));

    // If the wall is not claimed, it can be dug
    if(!isClaimed())
        return true;

    // It is claimed. If it is by the given seat, it can be dug
    if(isClaimedForSeat(seat))
        return true;

    return false;
}

bool Tile::isWallClaimable(Seat* seat)
{
    if (getFullness() == 0.0)
        return false;

    if (mType == TileType::lava || mType == TileType::water || mType == TileType::rock || mType == TileType::gold)
        return false;

    // Check whether at least one neighbor is a claimed ground tile of the given seat
    // which is a condition to permit claiming the given wall tile.
    bool foundClaimedGroundTile = false;
    for (Tile* tile : mNeighbors)
    {
        if (tile->getFullness() > 0.0)
            continue;

        if (!tile->isClaimedForSeat(seat))
            continue;

        foundClaimedGroundTile = true;
        break;
    }

    if (foundClaimedGroundTile == false)
        return false;

    // If the tile is not claimed, it is claimable
    if (!isClaimed())
        return true;

    // We check if the tile is already claimed for our seat
    if (isClaimedForSeat(seat))
        return false;

    // The tile is claimed by another team. We check if there is a claimed ground tile
    // claimed by the same team. If not, we can claim. If yes, we cannot
    Seat* tileSeat = getSeat();
    if (tileSeat == nullptr)
        return true;

    foundClaimedGroundTile = false;
    for (Tile* tile : mNeighbors)
    {
        if (tile->getFullness() > 0.0)
            continue;

        if (!tile->isClaimedForSeat(tileSeat))
            continue;

        foundClaimedGroundTile = true;
        break;
    }

    if (foundClaimedGroundTile)
        return false;

    return true;
}

bool Tile::isWallClaimedForSeat(Seat* seat)
{
    if (getFullness() == 0.0)
        return false;

    if (mClaimedPercentage < 1.0)
        return false;

    Seat* tileSeat = getSeat();
    if(tileSeat == nullptr)
        return false;

    if (tileSeat->canOwnedTileBeClaimedBy(seat))
        return false;

    return true;
}

std::string Tile::getFormat()
{
    return "posX\tposY\ttype\tfullness\tseatId(optional)";
}

void Tile::exportToStream(std::ostream& os) const
{
    os << getX() << "\t" << getY() << "\t";
    os << getType() << "\t" << getFullness();
    if(getSeat() == nullptr)
        return;

    os << "\t" << getSeat()->getId();
}

void Tile::exportToPacket(ODPacket& os) const
{
    //Check to make sure this function is not called. Seat argument should be used instead
    OD_ASSERT_TRUE_MSG(false, "tile=" + displayAsString(this));
    throw std::runtime_error("ERROR: Wrong packet export function used for tile");
}


ODPacket& operator<<(ODPacket& os, const TileType& type)
{
    uint32_t intType = static_cast<uint32_t>(type);
    os << intType;
    return os;
}

ODPacket& operator>>(ODPacket& is, TileType& type)
{
    uint32_t intType;
    is >> intType;
    type = static_cast<TileType>(intType);
    return is;
}

std::ostream& operator<<(std::ostream& os, const TileType& type)
{
    uint32_t intType = static_cast<uint32_t>(type);
    os << intType;
    return os;
}

std::istream& operator>>(std::istream& is, TileType& type)
{
    uint32_t intType;
    is >> intType;
    type = static_cast<TileType>(intType);
    return is;
}

ODPacket& operator<<(ODPacket& os, const TileVisual& type)
{
    uint32_t intType = static_cast<uint32_t>(type);
    os << intType;
    return os;
}

ODPacket& operator>>(ODPacket& is, TileVisual& type)
{
    uint32_t intType;
    is >> intType;
    type = static_cast<TileVisual>(intType);
    return is;
}

std::ostream& operator<<(std::ostream& os, const TileVisual& type)
{
    uint32_t intType = static_cast<uint32_t>(type);
    os << intType;
    return os;
}

std::istream& operator>>(std::istream& is, TileVisual& type)
{
    uint32_t intType;
    is >> intType;
    type = static_cast<TileVisual>(intType);
    return is;
}

std::string Tile::tileTypeToString(TileType t)
{
    switch (t)
    {
        case TileType::dirt:
            return "Dirt";

        case TileType::rock:
            return "Rock";

        case TileType::gold:
            return "Gold";

        case TileType::water:
            return "Water";

        case TileType::lava:
            return "Lava";

        default:
            return "Unknown tile type=" + Helper::toString(static_cast<uint32_t>(t));
    }
}

std::string Tile::tileVisualToString(TileVisual tileVisual)
{
    switch (tileVisual)
    {
        case TileVisual::nullTileVisual:
            return "nullTileVisual";

        case TileVisual::dirtGround:
            return "dirtGround";

        case TileVisual::dirtFull:
            return "dirtFull";

        case TileVisual::rockGround:
            return "rockGround";

        case TileVisual::rockFull:
            return "rockFull";

        case TileVisual::goldGround:
            return "goldGround";

        case TileVisual::goldFull:
            return "goldFull";

        case TileVisual::waterGround:
            return "waterGround";

        case TileVisual::lavaGround:
            return "lavaGround";

        case TileVisual::claimedGround:
            return "claimedGround";

        case TileVisual::claimedFull:
            return "claimedFull";

        default:
            return "Unknown tile type=" + Helper::toString(static_cast<uint32_t>(tileVisual));
    }
}

TileVisual Tile::tileVisualFromString(const std::string& strTileVisual)
{
    uint32_t nb = static_cast<uint32_t>(TileVisual::countTileVisual);
    for(uint32_t k = 0; k < nb; ++k)
    {
        TileVisual tileVisual = static_cast<TileVisual>(k);
        if(strTileVisual.compare(tileVisualToString(tileVisual)) == 0)
            return tileVisual;
    }

    return TileVisual::nullTileVisual;
}

int Tile::nextTileFullness(int f)
{

    // Cycle the tile's fullness through the possible values
    switch (f)
    {
        case 0:
            return 25;

        case 25:
            return 50;

        case 50:
            return 75;

        case 75:
            return 100;

        case 100:
            return 0;

        default:
            return 0;
    }
}

void Tile::setMarkedForDigging(bool ss, const Player *pp)
{
    /* If we are trying to mark a tile that is not dirt or gold
     * or is already dug out, ignore the request.
     */
    if (ss && !isDiggable(pp->getSeat()))
        return;

    // If the tile was already in the given state, we can return
    if (getMarkedForDigging(pp) == ss)
        return;

    if (ss)
        addPlayerMarkingTile(pp);
    else
        removePlayerMarkingTile(pp);
}

bool Tile::getMarkedForDigging(const Player *p) const
{
    if(std::find(mPlayersMarkingTile.begin(), mPlayersMarkingTile.end(), p) != mPlayersMarkingTile.end())
        return true;

    return false;
}

bool Tile::isMarkedForDiggingByAnySeat()
{
    return !mPlayersMarkingTile.empty();
}

void Tile::addPlayerMarkingTile(const Player *p)
{
    mPlayersMarkingTile.push_back(p);
}

void Tile::removePlayerMarkingTile(const Player *p)
{
    auto it = std::find(mPlayersMarkingTile.begin(), mPlayersMarkingTile.end(), p);
    if(it == mPlayersMarkingTile.end())
        return;

    mPlayersMarkingTile.erase(it);
}

void Tile::addNeighbor(Tile *n)
{
    mNeighbors.push_back(n);
}

double Tile::scaleDigRate(double digRate)
{
    if(!isClaimed())
        return digRate;

    return 0.2 * digRate;
}

Tile* Tile::getNeighbor(unsigned int index)
{
    OD_ASSERT_TRUE_MSG(index < mNeighbors.size(), "tile=" + displayAsString(this));
    if(index >= mNeighbors.size())
        return nullptr;

    return mNeighbors[index];
}

std::string Tile::buildName(int x, int y)
{
    std::stringstream ss;
    ss << TILE_PREFIX;
    ss << x;
    ss << "_";
    ss << y;
    return ss.str();
}

bool Tile::checkTileName(const std::string& tileName, int& x, int& y)
{
    if (tileName.compare(0, TILE_PREFIX.length(), TILE_PREFIX) != 0)
        return false;

    if(sscanf(tileName.c_str(), TILE_SCANF.c_str(), &x, &y) != 2)
        return false;

    return true;
}

std::string Tile::toString(FloodFillType type)
{
    return Helper::toString(toUInt32(type));
}

bool Tile::isFloodFillFilled() const
{
    if(getFullness() > 0.0)
        return true;

    switch(getType())
    {
        case TileType::dirt:
        case TileType::gold:
        case TileType::rock:
        {
            if((mFloodFillColor[toUInt32(FloodFillType::ground)] != -1) &&
               (mFloodFillColor[toUInt32(FloodFillType::groundWater)] != -1) &&
               (mFloodFillColor[toUInt32(FloodFillType::groundLava)] != -1) &&
               (mFloodFillColor[toUInt32(FloodFillType::groundWaterLava)] != -1))
            {
                return true;
            }
            break;
        }
        case TileType::water:
        {
            if((mFloodFillColor[toUInt32(FloodFillType::groundWater)] != -1) &&
               (mFloodFillColor[toUInt32(FloodFillType::groundWaterLava)] != -1))
            {
                return true;
            }
            break;
        }
        case TileType::lava:
        {
            if((mFloodFillColor[toUInt32(FloodFillType::groundLava)] != -1) &&
               (mFloodFillColor[toUInt32(FloodFillType::groundWaterLava)] != -1))
            {
                return true;
            }
            break;
        }
        default:
            return true;
    }

    return false;
}

bool Tile::isSameFloodFill(FloodFillType type, Tile* tile) const
{
    return mFloodFillColor[toUInt32(type)] == tile->mFloodFillColor[toUInt32(type)];
}

void Tile::resetFloodFill()
{
    for(int& floodFillValue : mFloodFillColor)
    {
        floodFillValue = -1;
    }
}

int Tile::floodFillValue(FloodFillType type) const
{
    uint32_t intFloodFill = toUInt32(type);
    OD_ASSERT_TRUE_MSG(intFloodFill < mFloodFillColor.size(), Helper::toString(intFloodFill));
    if(intFloodFill >= mFloodFillColor.size())
        return -1;

    return mFloodFillColor[intFloodFill];
}

bool Tile::updateFloodFillFromTile(FloodFillType type, Tile* tile)
{
    if((floodFillValue(type) != -1) ||
       (tile->floodFillValue(type) == -1))
    {
        return false;
    }

    mFloodFillColor[toUInt32(type)] = tile->mFloodFillColor[toUInt32(type)];
    return true;
}

void Tile::replaceFloodFill(FloodFillType type, int newValue)
{
    mFloodFillColor[toUInt32(type)] = newValue;
}

void Tile::logFloodFill() const
{
    std::string str = "Tile floodfill : " + Tile::displayAsString(this)
        + " - fullness=" + Helper::toString(getFullness())
        + " - seatId=" + std::string(getSeat() == nullptr ? "-1" : Helper::toString(getSeat()->getId()));
    int cpt = 0;
    for(const int& floodFill : mFloodFillColor)
    {
        str += ", [" + Helper::toString(cpt) + "]=" +
            Helper::toString(floodFill);
        ++cpt;
    }
    LogManager::getSingleton().logMessage(str);
}

bool Tile::isClaimedForSeat(const Seat* seat) const
{
    if(!isClaimed())
        return false;

    if(getSeat()->canOwnedTileBeClaimedBy(seat))
        return false;

    return true;
}

bool Tile::isClaimed() const
{
    if(!getGameMap()->isServerGameMap())
        return ((mTileVisual == TileVisual::claimedGround) || (mTileVisual == TileVisual::claimedFull));

    if(getSeat() == nullptr)
        return false;

    if(mClaimedPercentage < 1.0)
        return false;

    return true;
}

void Tile::clearVision()
{
    mSeatsWithVision.clear();
}

void Tile::notifyVision(Seat* seat)
{
    if(std::find(mSeatsWithVision.begin(), mSeatsWithVision.end(), seat) != mSeatsWithVision.end())
        return;

    seat->notifyVisionOnTile(this);
    mSeatsWithVision.push_back(seat);

    // We also notify vision for allied seats
    for(Seat* alliedSeat : seat->getAlliedSeats())
        notifyVision(alliedSeat);
}

void Tile::setSeats(const std::vector<Seat*>& seats)
{
    mTileChangedForSeats.clear();
    for(Seat* seat : seats)
    {
        // Every tile should be notified by default
        std::pair<Seat*, bool> p(seat, true);
        mTileChangedForSeats.push_back(p);
    }
}

bool Tile::hasChangedForSeat(Seat* seat) const
{
    for(const std::pair<Seat*, bool>& seatChanged : mTileChangedForSeats)
    {
        if(seatChanged.first != seat)
            continue;

        return seatChanged.second;
    }
    OD_ASSERT_TRUE_MSG(false, "Unknown seat id=" + Ogre::StringConverter::toString(seat->getId()));
    return false;
}

void Tile::changeNotifiedForSeat(Seat* seat)
{
    for(std::pair<Seat*, bool>& seatChanged : mTileChangedForSeats)
    {
        if(seatChanged.first != seat)
            continue;

        seatChanged.second = false;
        break;
    }
}

void Tile::computeTileVisual()
{
    if(isClaimed())
    {
        if(mFullness > 0.0)
            mTileVisual = TileVisual::claimedFull;
        else
            mTileVisual = TileVisual::claimedGround;
        return;
    }

    switch(getType())
    {
        case TileType::dirt:
            if(mFullness > 0.0)
                mTileVisual = TileVisual::dirtFull;
            else
                mTileVisual = TileVisual::dirtGround;
            return;

        case TileType::rock:
            if(mFullness > 0.0)
                mTileVisual = TileVisual::rockFull;
            else
                mTileVisual = TileVisual::rockGround;
            return;

        case TileType::gold:
            if(mFullness > 0.0)
                mTileVisual = TileVisual::goldFull;
            else
                mTileVisual = TileVisual::goldGround;
            return;

        case TileType::water:
            mTileVisual = TileVisual::waterGround;
            return;

        case TileType::lava:
            mTileVisual = TileVisual::lavaGround;
            return;

        default:
            mTileVisual = TileVisual::nullTileVisual;
            return;
    }
}

std::string Tile::displayAsString(const Tile* tile)
{
    return "[" + Ogre::StringConverter::toString(tile->getX()) + ","
         + Ogre::StringConverter::toString(tile->getY())+ "]";
}
