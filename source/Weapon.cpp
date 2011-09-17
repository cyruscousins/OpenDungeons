#include "Weapon.h"
#include "RenderRequest.h"
#include "Creature.h"
#include "RenderManager.h"

void Weapon::createMesh()
{
    if (isMeshExisting())
    {
        return;
    }

    setMeshExisting(true);

    if (getName().compare("none") == 0)
    {
        return;
    }

    RenderRequest *request = new RenderRequest;
    request->type = RenderRequest::createWeapon;
    request->p = this;
    request->p2 = parentCreature;
    request->p3 = &handString;

    // Add the request to the queue of rendering operations to be performed before the next frame.
    RenderManager::queueRenderRequest(request);
}

void Weapon::destroyMesh()
{
    if (!isMeshExisting())
    {
        return;
    }

    setMeshExisting(false);

    RenderRequest *request = new RenderRequest;
    request->type = RenderRequest::destroyWeapon;
    request->p = this;
    request->p2 = parentCreature;

    // Add the request to the queue of rendering operations to be performed before the next frame.
    RenderManager::queueRenderRequest(request);
}

void Weapon::deleteYourself()
{
    if (isMeshExisting())
    {
        destroyMesh();
    }

    // Create a render request asking the render queue to actually do the deletion of this creature.
    RenderRequest *request = new RenderRequest;
    request->type = RenderRequest::deleteWeapon;
    request->p = this;

    // Add the requests to the queue of rendering operations to be performed before the next frame.
    RenderManager::queueRenderRequest(request);
}

std::string Weapon::getFormat()
{
    //NOTE:  When this format changes changes to RoomPortal::spawnCreature() may be necessary.
    return "name\tdamage\trange\tdefense";
}

std::ostream& operator<<(std::ostream& os, Weapon *w)
{
    os << w->getName() << "\t" << w->damage << "\t" << w->range << "\t" << w->defense;
    return os;
}

std::istream& operator>>(std::istream& is, Weapon *w)
{
    std::string name;
    is >> name >> w->damage >> w->range >> w->defense;
    w->setName(name);
    w->setMeshName(name + ".mesh");

    return is;
}
