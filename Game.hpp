#pragma once

#include <glm/glm.hpp>

#include <string>
#include <list>
#include <random>
#include <vector>

struct Connection;

// Game state, separate from rendering.

// Currently set up for a "client sends controls" / "server sends whole state" situation.

enum class Message : uint8_t
{
	C2S_Controls = 1, // Greg!
	S2C_State = 's',
	//...
};

enum class Role : uint8_t
{
	Fly,
	Human
};

// used to represent a control input:
struct Button
{
	uint8_t downs = 0;		// times the button has been pressed
	bool pressed = false; // is the button pressed now
};

// state of one player in the game:
struct Player
{
	// player inputs (sent from client):
	struct Controls
	{
		Button left, right, up, down, space, fly, human, start;

		// Camera angle
		float horiz = 0.0f;
		float vert = 0.0f;

		void send_controls_message(Connection *connection) const;

		// returns 'false' if no message or not a controls message,
		// returns 'true' if read a controls message,
		// throws on malformed controls message
		bool recv_controls_message(Connection *connection);
	} controls;

	Role role = Role::Human;

	// player state (sent from server):
	glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 velocity = glm::vec3(0.0f, 0.0f, 0.0f);

	// offset, only used for erratic fly movement
	glm::vec3 offset = glm::vec3(0.0f, 0.0f, 0.0f);

	glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
	std::string name = "";
	uint32_t id = 0;

	float vert = 0.0f;
	float horiz = 0.0f;
	float speed = 0.0f;
	float turnDip = 0.0f;

	// Fly Specific Variables
	bool alive = true;

	// Human Specific Variables
	float swat_time = 0.0f;
	float swat_cooldown = 0.0f;
};

struct Game
{
	std::list<Player> players;		//(using list so they can have stable addresses)
	Player *spawn_player();				// add player the end of the players list (may also, e.g., play some spawn anim)
	void remove_player(Player *); // remove player from game (may also, e.g., play some despawn anim)

	std::mt19937 mt;								 // used for spawning players
	uint32_t next_player_number = 1; // used for naming players

	int flies_remaining = 0;
	float time = 0.0f;
	float timeLimit = 300.0f;
	bool humanWon = false;
	bool started = false;
	uint32_t count(Role role) const;

	std::vector<glm::vec3> triangles;

	Game();

	// state update function:
	void update(float elapsed);

	// constants:
	// the update rate on the server:
	inline static constexpr float Tick = 1.0f / 30.0f;

	// arena size:
	inline static constexpr glm::vec3 ArenaMin = glm::vec3(-6.0f, -6.0f, 0.0f);
	inline static constexpr glm::vec3 ArenaMax = glm::vec3(6.0f, 6.0f, 3.0f);

	// Human constants:
	// 0.5x0.5x2
	inline static constexpr float HumanRadius = 0.3f;
	inline static constexpr float HumanSpeed = 4.0f;
	inline static constexpr float HumanAccelHalflife = 0.2f;
	inline static constexpr float HumanHeight = 2.0f;

	// Fly constants:
	inline static constexpr float FlyRadius = 0.075f;
	inline static constexpr float FlySpeed = 5.0f;
	inline static constexpr float FlyAccelHalflife = 0.25f;
	inline static constexpr float TurnSpeed = 3.0f;
	inline static constexpr float Accel = 2.0f;
	inline static constexpr float Decel = 3.0f;
	inline static constexpr float HorizRate = 2.5f;
	inline static constexpr float VertRate = 1.5f;

	inline static constexpr glm::vec3 FlyNoise1Freq = glm::vec3(1.0f / 3.0f, 1.0f / 7.0f, 1.0f / 5.0f);
	inline static constexpr glm::vec3 FlyNoise2Freq = glm::vec3(1.0f / 11.0f, 1.0f / 2.3f, 1.0f / 3.14159f);
	inline static constexpr float FlyNoiseAmplitude = 0.25f;

	//---- communication helpers ----

	// used by client:
	// set game state from data in connection buffer
	//  (return true if data was read)
	bool recv_state_message(Connection *connection);

	// used by server:
	// send game state.
	//   Will move "connection_player" to the front of the front of the sent list.
	void send_state_message(Connection *connection, Player *connection_player = nullptr) const;
};
