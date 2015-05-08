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

#ifndef MODEMANAGER_H
#define MODEMANAGER_H

#include "modes/AbstractModeManager.h"

#include "modes/InputManager.h"

#include <vector>
#include <string>
#include <memory>

class AbstractApplicationMode;
class CameraManager;
class Gui;

namespace Ogre {
  class RenderWindow;
}

class ModeManager final : public AbstractModeManager
{
public:

    ModeManager(Ogre::RenderWindow* renderWindow, Gui* gui);
    virtual ~ModeManager();

    //! \brief Updates mouse event, checks for made changes, ...
    void update(const Ogre::FrameEvent& evt);

    AbstractApplicationMode* getCurrentMode();
    ModeType getCurrentModeType() const;

    //! \brief Request loading the given game mode
    void requestMode(ModeType mode)
    {
        mRequestedMode = mode;
    }

    InputManager& getInputManager()
    {
        return mInputManager;
    }

    Gui& getGui()
    {
        return *mGui;
    }

private:
    //! \brief The common input manager instance
    InputManager mInputManager;

    //! \brief Pointer to the GUI instance
    Gui* mGui;

    //! \brief The currently loaded mode.
    std::unique_ptr<AbstractApplicationMode> mCurrentApplicationMode;

    //! \brief Tells which new mode is requested.
    ModeManager::ModeType mRequestedMode;

    //! \brief Actually change the mode if needed
    void checkModeChange();
};

#endif // MODEMANAGER_H
