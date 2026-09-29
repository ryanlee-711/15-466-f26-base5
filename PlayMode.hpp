#include "Mode.hpp"

#include "Connection.hpp"
#include "Game.hpp"
#include "Scene.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <map>
#include <memory>
#include <set>

struct PlayMode : Mode
{
	PlayMode();
	virtual ~PlayMode();

	// functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	// input tracking for local player:
	Player::Controls controls;

	// latest game state (from server):
	Game game;

	// last message from server:
	std::string server_message;

	Client *client = nullptr;
	SDL_Process *server = nullptr;
	enum
	{
		Menu,
		Join,
		Lobby
	} screen = Menu;
	std::string ip;

	float mouse_sen = 2.5f;

	Scene scene;
	Scene::Camera *camera = nullptr;
	Scene fly, player;
	Scene::Transform *fly_root = nullptr;
	Scene::Transform *player_root = nullptr;
	Scene::Transform *arm = nullptr;
	glm::quat arm_base;
	Scene::Transform *leg_l = nullptr;
	Scene::Transform *leg_r = nullptr;
	glm::quat leg_l_base, leg_r_base;
	float walk_time = 0.0f;
	glm::vec3 fly_cam_offset;
	glm::quat fly_cam_rotation;

	// Audio
	std::shared_ptr<Sound::PlayingSample> menu_music;
	std::shared_ptr<Sound::PlayingSample> main_music;
	std::shared_ptr<Sound::PlayingSample> timer_music;
	float OST_VOLUME = 0.3f;

	std::map<uint32_t, std::shared_ptr<Sound::PlayingSample>> fly_buzz;
	std::map<uint32_t, std::shared_ptr<Sound::PlayingSample>> fly_dead;
	std::map<uint32_t, std::shared_ptr<Sound::PlayingSample>> player_steps;
	std::map<uint32_t, std::shared_ptr<Sound::PlayingSample>> player_swats;

	std::map<uint32_t, float> player_step_cooldown_times;
	std::map<uint32_t, float> player_last_swat_times;
	float player_step_cooldown = 0.3f;
};
