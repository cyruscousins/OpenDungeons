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

#include "MenuMode.h"

#include "ODApplication.h"
#include "gamemap/GameMap.h"
#include "modes/ModeManager.h"
#include "render/Gui.h"
#include "render/ODFrameListener.h"
#include "render/TextRenderer.h"
#include "sound/MusicPlayer.h"

#include <CEGUI/widgets/PushButton.h>

namespace
{
    //FIXME: Rename this into mainmenumode.
//! \brief Helper functor to change modes
class ModeChanger
{
public:
    bool operator()(const CEGUI::EventArgs& e)
    {
        mMode->changeModeEvent(mNewMode, e);
        return true;
    }

    MenuMode* mMode;
    AbstractModeManager::ModeType mNewMode;
};
}

MenuMode::MenuMode(ModeManager *modeManager):
    AbstractApplicationMode(modeManager, ModeManager::MAIN_MENU)
{
    connectModeChangeEvent(Gui::MM_BUTTON_MAPEDITOR, AbstractModeManager::ModeType::MENU_EDITOR);
    connectModeChangeEvent(Gui::MM_BUTTON_START_SKIRMISH, AbstractModeManager::ModeType::MENU_SKIRMISH);
    connectModeChangeEvent(Gui::MM_BUTTON_START_REPLAY, AbstractModeManager::ModeType::MENU_REPLAY);
    connectModeChangeEvent(Gui::MM_BUTTON_START_MULTIPLAYER_CLIENT, AbstractModeManager::ModeType::MENU_MULTIPLAYER_CLIENT);
    connectModeChangeEvent(Gui::MM_BUTTON_START_MULTIPLAYER_SERVER, AbstractModeManager::ModeType::MENU_MULTIPLAYER_SERVER);
    connectModeChangeEvent(Gui::MM_BUTTON_LOAD_GAME, AbstractModeManager::ModeType::MENU_LOAD_SAVEDGAME);
    addEventConnection(
        getModeManager().getGui().getGuiSheet(Gui::mainMenu)->getChild(Gui::MM_BUTTON_QUIT)->subscribeEvent(
            CEGUI::PushButton::EventClicked,
            CEGUI::Event::Subscriber(&MenuMode::quitButtonPressed, this)
        )
    );
}

void MenuMode::activate()
{
    // Loads the corresponding Gui sheet.
    getModeManager().getGui().loadGuiSheet(Gui::mainMenu);

    giveFocus();

    TextRenderer::getSingleton().setText(ODApplication::POINTER_INFO_STRING, "");

    // Play the main menu music
    // TODO: Make this configurable.
    MusicPlayer::getSingleton().play("OpenDungeonsMainTheme_pZi.ogg");

    GameMap* gameMap = ODFrameListener::getSingletonPtr()->getClientGameMap();
    gameMap->clearAll();
    gameMap->setGamePaused(true);
}

void MenuMode::connectModeChangeEvent(const std::string& buttonName, AbstractModeManager::ModeType mode)
{
    CEGUI::Window* window = getModeManager().getGui().getGuiSheet(Gui::mainMenu);

    addEventConnection(
        window->getChild(buttonName)->subscribeEvent(
          CEGUI::PushButton::EventClicked,
          CEGUI::Event::Subscriber(ModeChanger{this, mode})
        )
    );
}

bool MenuMode::quitButtonPressed(const CEGUI::EventArgs&)
{
    ODFrameListener::getSingletonPtr()->requestExit();
    return true;
}
