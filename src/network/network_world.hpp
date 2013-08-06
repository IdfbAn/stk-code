#ifndef NETWORK_WORLD_HPP
#define NETWORK_WORLD_HPP

#include "network/singleton.hpp"
#include "input/input.hpp"
#include <map>

class Controller;
class KartUpdateProtocol;
class AbstractKart;
class Item;

/*! \brief Manages the world updates during an online game
 *  This function's update is to be called instead of the normal World update
*/
class NetworkWorld : public Singleton<NetworkWorld>
{
    friend class Singleton<NetworkWorld>;
    public:
        void update(float dt);

        void start() { m_running = true; }
        void stop() { m_running = false; }
        bool isRunning() { return m_running; }
        bool isRaceOver();

        void collectedItem(Item *item, AbstractKart *kart);
        void controllerAction(Controller* controller, PlayerAction action, int value);

        std::string m_self_kart;
    protected:
        bool m_running;
        float m_race_time;
        bool m_has_run;

    private:
        NetworkWorld();
        virtual ~NetworkWorld();
};

#endif // NETWORK_WORLD_HPP
