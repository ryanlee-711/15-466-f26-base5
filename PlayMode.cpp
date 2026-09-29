#include "PlayMode.hpp"

#include "DrawLines.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"
#include "Mesh.hpp"
#include "LitColorTextureProgram.hpp"
#include "Sound.hpp"

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <random>
#include <array>
#include <algorithm>

GLuint meshes_for_lit_color_texture_program = 0;
Load<MeshBuffer> meshes(LoadTagDefault, []() -> MeshBuffer const *
												{
	MeshBuffer const *ret = new MeshBuffer(data_path("flieger-war.pnct"));
	meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret; });

static void add_drawable(Scene &scene, Scene::Transform *transform, std::string const &mesh_name)
{
	if (mesh_name.find("_BoundingBox") != std::string::npos)
		return;
	Mesh const &mesh = meshes->lookup(mesh_name);
	scene.drawables.emplace_back(transform);
	Scene::Drawable &drawable = scene.drawables.back();
	drawable.pipeline = lit_color_texture_program_pipeline;
	drawable.pipeline.vao = meshes_for_lit_color_texture_program;
	drawable.pipeline.type = mesh.type;
	drawable.pipeline.start = mesh.start;
	drawable.pipeline.count = mesh.count;
}

// Music
static Load<Sound::Sample> music_base_sample(LoadTagDefault, []() -> Sound::Sample const *
																						 { return new Sound::Sample(data_path("main_theme_base.wav")); });

static Load<Sound::Sample> music_timer_sample(LoadTagDefault, []() -> Sound::Sample const *
																							{ return new Sound::Sample(data_path("main_theme_timer.wav")); });

static Load<Sound::Sample> menu_sample(LoadTagDefault, []() -> Sound::Sample const *
																			 { return new Sound::Sample(data_path("menu_theme.wav")); });

// SFX
static Load<Sound::Sample> fly_buzz_sample(LoadTagDefault, []() -> Sound::Sample const *
																					 { return new Sound::Sample(data_path("Fly_Buzz.wav")); });

static Load<Sound::Sample> fly_death_sample(LoadTagDefault, []() -> Sound::Sample const *
																						{ return new Sound::Sample(data_path("Fly_Death.wav")); });

static Load<Sound::Sample> player_swat(LoadTagDefault, []() -> Sound::Sample const *
																			 { return new Sound::Sample(data_path("Player_Swat.wav")); });

static Load<Sound::Sample> player_step(LoadTagDefault, []() -> Sound::Sample const *
																			 { return new Sound::Sample(data_path("Player_Step.wav")); });

Load<Scene> room_scene(LoadTagDefault, []() -> Scene const *
											 { return new Scene(data_path("room.scene"), add_drawable); });
Load<Scene> fly_scene(LoadTagDefault, []() -> Scene const *
											{ return new Scene(data_path("fly.scene"), add_drawable); });
Load<Scene> player_scene(LoadTagDefault, []() -> Scene const *
												 { return new Scene(data_path("player.scene"), add_drawable); });

PlayMode::PlayMode() : scene(*room_scene), fly(*fly_scene), player(*player_scene)
{
	for (auto &t : fly.transforms)
	{
		if (t.name == "Fly")
			fly_root = &t;
		if (t.name == "Wing_L")
			wing_l = &t;
		if (t.name == "Wing_R")
			wing_r = &t;
	}
	for (auto &t : player.transforms)
	{
		if (t.name == "Player")
			player_root = &t;
		if (t.name == "Arm_R")
			arm = &t;
		if (t.name == "Leg_L")
			leg_l = &t;
		if (t.name == "Leg_R")
			leg_r = &t;
	}
	arm_base = arm->rotation;
	leg_l_base = leg_l->rotation;
	leg_r_base = leg_r->rotation;
	wing_l_base = wing_l->rotation;
	wing_r_base = wing_r->rotation;
	fly_cam_offset = fly.cameras.front().transform->position;
	fly_cam_rotation = fly.cameras.front().transform->rotation;
	player.cameras.front().transform->parent = player_root;
	player.cameras.front().fovy = glm::radians(70.0f);

	menu_music = Sound::loop(*menu_sample, 0.0f);

	scene.transforms.emplace_back();
	Scene::Transform *map = &scene.transforms.back();
	map->scale = Game::MapScale;
	for (auto &t : scene.transforms)
		if (t.parent == nullptr && &t != map)
			t.parent = map;
}

PlayMode::~PlayMode()
{
	if (menu_music)
		menu_music->stop(0.2f);
	if (main_music)
		main_music->stop(0.2f);
	if (timer_music)
		timer_music->stop(0.2f);
	for (auto &[id, buzz] : fly_buzz)
		if (buzz)
			buzz->stop(0.2f);
	if (server)
		SDL_KillProcess(server, true);
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size)
{
	if (!game.started && evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
	{
		int row = int(10.0f * evt.button.y / float(window_size.y));
		if (screen == Menu && row == 4)
			screen = Join;
		else if (screen == Menu && row == 5)
		{
			std::string path = data_path("server");
			char const *args[] = {path.c_str(), "1337", nullptr};
			server = SDL_CreateProcess(args, false);
			screen = Lobby;
		}
		else if (screen == Menu && row == 6)
			Mode::set_current(nullptr);
		else if (screen == Lobby && row == 4)
			controls.fly.downs += 1;
		else if (screen == Lobby && row == 5)
			controls.human.downs += 1;
		else if (screen == Lobby && row == 7)
		{
			try
			{
				// timeInput clamped to 3 characters so this shouldn't overflow
				controls.timeLimit = glm::clamp(std::stoi(timeInput), 30, 300);
			}
			catch (...)
			{
				// Do nothing if conversion fails
			}
			controls.start.downs += 1;
		}
		return true;
	}
	if (screen == Join && evt.type == SDL_EVENT_KEY_DOWN)
	{
		if (evt.key.key == SDLK_BACKSPACE && !ip.empty())
			ip.pop_back();
		else if (evt.key.key == SDLK_PERIOD || (evt.key.key >= SDLK_0 && evt.key.key <= SDLK_9))
			ip += char(evt.key.key);
		else if (evt.key.key == SDLK_RETURN)
		{
			try
			{
				client = new Client(ip, "1337");
				screen = Lobby;
			}
			catch (std::exception const &)
			{
			}
		}
		return true;
	}

	// Check server -- only host can set time limit
	if (screen == Lobby && server && evt.type == SDL_EVENT_KEY_DOWN)
	{
		if (evt.key.key == SDLK_BACKSPACE && !timeInput.empty())
			timeInput.pop_back();
		else if (evt.key.key >= SDLK_0 && evt.key.key <= SDLK_9 && timeInput.length() < 3)
			timeInput += char(evt.key.key);
	}

	if (evt.type == SDL_EVENT_KEY_DOWN)
	{
		if (evt.key.repeat)
		{
			// ignore repeats
		}
		else if (evt.key.key == SDLK_A)
		{
			controls.left.downs += 1;
			controls.left.pressed = true;
			return true;
		}
		else if (evt.key.key == SDLK_D)
		{
			controls.right.downs += 1;
			controls.right.pressed = true;
			return true;
		}
		else if (evt.key.key == SDLK_W)
		{
			controls.up.downs += 1;
			controls.up.pressed = true;
			return true;
		}
		else if (evt.key.key == SDLK_S)
		{
			controls.down.downs += 1;
			controls.down.pressed = true;
			return true;
		}
		else if (evt.key.key == SDLK_SPACE)
		{
			controls.space.downs += 1;
			controls.space.pressed = true;
			return true;
		}
		else if (evt.key.key == SDLK_LSHIFT)
		{
			controls.shift.downs += 1;
			controls.shift.pressed = true;
			return true;
		}
		else if (evt.key.key == SDLK_ESCAPE)
		{
			SDL_SetWindowRelativeMouseMode(Mode::window, false);
			return true;
		}
	}
	else if (evt.type == SDL_EVENT_KEY_UP)
	{
		if (evt.key.key == SDLK_A)
		{
			controls.left.pressed = false;
			return true;
		}
		else if (evt.key.key == SDLK_D)
		{
			controls.right.pressed = false;
			return true;
		}
		else if (evt.key.key == SDLK_W)
		{
			controls.up.pressed = false;
			return true;
		}
		else if (evt.key.key == SDLK_S)
		{
			controls.down.pressed = false;
			return true;
		}
		else if (evt.key.key == SDLK_SPACE)
		{
			controls.space.pressed = false;
			return true;
		}
		else if (evt.key.key == SDLK_LSHIFT)
		{
			controls.shift.pressed = false;
			return true;
		}
	}
	else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
	{
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == false)
		{
			SDL_SetWindowRelativeMouseMode(Mode::window, true);
			return true;
		}
	}
	else if (evt.type == SDL_EVENT_MOUSE_MOTION)
	{
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == true)
		{
			glm::vec2 motion = glm::vec2(
					evt.motion.xrel / float(window_size.y),
					-evt.motion.yrel / float(window_size.y));
			controls.horiz += -motion.x * camera->fovy * mouse_sen;
			controls.vert += motion.y * camera->fovy * mouse_sen;
			controls.vert = glm::clamp(controls.vert, -1.4f, 1.4f);

			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed)
{
	walk_time += elapsed;

	// Menu music
	if (!game.started)
	{
		if (!menu_music)
			menu_music = Sound::loop(*menu_sample, 0.0f);

		menu_music->set_volume(OST_VOLUME, 0.5f);

		if (main_music)
		{
			main_music->stop(0.5f);
			main_music = nullptr;
		}

		if (timer_music)
		{
			timer_music->stop(0.5f);
			timer_music = nullptr;
		}

		for (auto &[id, buzz] : fly_buzz)
			if (buzz)
				buzz->stop(0.2f);

		// Empty old maps
		fly_buzz.clear();
		fly_dead.clear();
		player_step_cooldown_times.clear();
		player_last_swat_times.clear();
	}

	if (screen == Lobby && client == nullptr)
	{
		try
		{
			client = new Client("localhost", "1337");
			ip = my_ip();
		}
		catch (std::exception const &)
		{
		}
	}
	if (client == nullptr)
		return;

	// queue data for sending to server:
	controls.send_controls_message(&client->connection);

	// reset button press counters:
	controls.left.downs = 0;
	controls.right.downs = 0;
	controls.up.downs = 0;
	controls.down.downs = 0;
	controls.space.downs = 0;
	controls.shift.downs = 0;
	controls.fly.downs = 0;
	controls.human.downs = 0;
	controls.start.downs = 0;

	// Handle music
	if (game.started)
	{
		// Stop menu music
		if (menu_music)
		{
			menu_music->stop(0.5f);
			menu_music = nullptr;
		}

		// Start main and timer music if not started
		if (!main_music)
			main_music = Sound::loop(*music_base_sample, OST_VOLUME);
		if (!timer_music)
			timer_music = Sound::loop(*music_timer_sample, 0.0f);

		// Set timer volume based on timer duration
		float timer_volume = 1.0 - std::max(0.0f, game.timeLimit - game.time) / game.timeLimit;
		timer_music->set_volume(OST_VOLUME * timer_volume, 0.5f);

		for (auto const &p : game.players)
		{
			if (p.role == Role::Fly)
			{
				if (!p.alive)
				{
					// Stop fly buzz
					auto buzz = fly_buzz[p.id];
					if (buzz)
					{
						buzz->stop(0.15f);
						fly_buzz[p.id] = nullptr;
					}

					// Play death sound if not already played
					if (!fly_dead[p.id])
					{
						fly_dead[p.id] = Sound::play_3D(*fly_death_sample, 1.0f, p.position + p.offset);
					}
				}
				else
				{
					// Set fly buzz playing
					float speed = glm::length(p.velocity);

					auto &buzz = fly_buzz[p.id];
					if (!buzz)
						buzz = Sound::loop_3D(*fly_buzz_sample, 0.0f, p.position + p.offset);

					buzz->set_position(p.position + p.offset);

					float buzz_volume = std::clamp(speed / Game::FlySpeed, 0.0f, 1.0f);
					// Set volume
					buzz->set_volume(0.25f * buzz_volume);
				}
			}
			else
			{
				// Compare to last stored swat time for this player to see if a swat just happened
				float &last_swat_time = player_last_swat_times[p.id];
				if (p.swat_time > last_swat_time)
				{
					// Play swat sound
					player_swats[p.id] = Sound::play_3D(*player_swat, 1.0f, p.position + Game::SwatEye);
				}
				last_swat_time = p.swat_time;

				// Check cooldown for step
				float &step_cooldown = player_step_cooldown_times[p.id];
				float speed = glm::length(p.velocity) / Game::HumanSpeed;

				if (step_cooldown <= 0.0f && speed > 0.2f)
				{
					// play step audio and reset cooldown
					float step_volume = std::clamp(speed, 0.0f, 1.0f);

					player_steps[p.id] = Sound::play_3D(*player_step, step_volume, p.position);
					step_cooldown = player_step_cooldown;
				}

				step_cooldown -= elapsed;
			}
		}
	}

	// send/receive data:
	client->poll([this](Connection *c, Connection::Event event)
							 {
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			//std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush(); //DEBUG
			bool handled_message;
			try {
				do {
					handled_message = false;
					if (game.recv_state_message(c)) handled_message = true;
				} while (handled_message);
			} catch (std::exception const &e) {
				std::cerr << "[" << c->socket << "] malformed message from server: " << e.what() << std::endl;
				//quit the game:
				throw e;
			}
		} }, 0.0);
}

void PlayMode::draw(glm::uvec2 const &drawable_size)
{
	if (!game.started)
	{
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_DEPTH_TEST);
		DrawLines lines(glm::mat4(
				1.0f / aspect, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f));
		auto row = [&](int r, std::string const &text)
		{
			lines.draw_text(text, glm::vec3(-0.8f, 0.85f - 0.2f * r, 0.0f), glm::vec3(0.1f, 0.0f, 0.0f), glm::vec3(0.0f, 0.1f, 0.0f));
		};
		if (screen == Menu)
		{
			row(2, "FLIEGER WAR");
			row(4, "Join Game");
			row(5, "Host Game");
			row(6, "Quit");
		}
		else if (screen == Join)
		{
			row(3, "Type host code, Enter to join:");
			row(4, ip + "_");
		}
		else if (game.players.empty())
		{
			row(4, "Connecting...");
		}
		else
		{
			row(1, "Code: " + ip);
			row(2, "Flies: " + std::to_string(game.count(Role::Fly)) + "  Humans: " + std::to_string(game.count(Role::Human)));
			row(3, std::string("You are: ") + (game.players.front().role == Role::Fly ? "Fly" : "Human"));
			row(4, "Be Fly");
			row(5, "Be Human");
			if (server)
			{
				row(6, "Enter time limit (default 200): " + timeInput + "_");
				if (game.count(Role::Fly) >= 1 && game.count(Role::Human) >= 1)
				{
					row(7, "Start Game");
				}
			}
		}
		GL_ERRORS();
		return;
	}

	auto place = [&](Player const &p) -> Scene &
	{
		if (p.role == Role::Human)
		{
			player_root->position = p.position;
			player_root->rotation = glm::angleAxis(p.horiz, glm::vec3(0.0f, 0.0f, 1.0f));
			player.cameras.front().transform->rotation = glm::angleAxis(glm::radians(90.0f) + p.vert, glm::vec3(1.0f, 0.0f, 0.0f));
			arm->rotation = glm::angleAxis(glm::radians(360.0f) * p.swat_time / Game::SwatDuration, glm::vec3(1.0f, 0.0f, 0.0f)) * arm_base;
			float swing = std::sin(walk_time * 10.0f) * 0.5f * std::min(1.0f, glm::length(p.velocity) / Game::HumanSpeed);
			leg_l->rotation = glm::angleAxis(swing, glm::vec3(1.0f, 0.0f, 0.0f)) * leg_l_base;
			leg_r->rotation = glm::angleAxis(-swing, glm::vec3(1.0f, 0.0f, 0.0f)) * leg_r_base;
			return player;
		}
		fly_root->position = p.position + p.offset;
		fly_root->rotation = glm::angleAxis(p.horiz, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::angleAxis(p.vert, glm::vec3(1.0f, 0.0f, 0.0f)) * glm::angleAxis(p.turnDip, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::quat yaw = glm::angleAxis(p.horiz, glm::vec3(0.0f, 0.0f, 1.0f));
		Scene::Transform *cam = fly.cameras.front().transform;
		cam->position = p.position + yaw * fly_cam_offset;
		cam->rotation = yaw * fly_cam_rotation;
		cam->position = p.position;
		for (float t = 0.025f; t <= 1.0f; t += 0.025f)
		{
			if (game.touches_wall(p.position + yaw * (t * fly_cam_offset), 0.075f))
				break;
			cam->position = p.position + yaw * (t * fly_cam_offset);
		}

		float flap = std::sin(walk_time * 50.0f) * 0.5f * std::min(1.0f, glm::length(p.velocity) / Game::FlySpeed);
		wing_l->rotation = glm::angleAxis(flap, glm::vec3(1.0f, 0.0f, 0.0f)) * wing_l_base;
		wing_r->rotation = glm::angleAxis(flap, glm::vec3(1.0f, 0.0f, 0.0f)) * wing_r_base;

		return fly;
	};

	Player const &me = game.players.front();
	camera = &place(me).cameras.front();
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);
	glm::mat4x3 camera_from_local = camera->transform->make_world_from_local();
	Sound::listener.set_position_right(camera_from_local[3], glm::normalize(camera_from_local[0]));
	glm::mat4 world_to_clip = camera->make_projection() * glm::mat4(camera->transform->make_local_from_world());

	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f, -1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	scene.draw(world_to_clip);
	for (auto const &p : game.players)
	{
		Scene &model = place(p);
		if (!p.alive)
			fly_root->rotation = glm::angleAxis(p.horiz, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		model.draw(world_to_clip);
	}

	{
		glDisable(GL_DEPTH_TEST);
		DrawLines lines(glm::mat4(1.0f));
		std::string text = std::to_string(int(game.timeLimit - game.time)) + "  Flies: " + std::to_string(game.flies_remaining);
		if (game.humanWon)
			text = "Humans win!";
		else if (game.time >= game.timeLimit)
			text = "Flies win!";
		lines.draw_text(text, glm::vec3(-0.95f, 0.85f, 0.0f), glm::vec3(0.06f, 0.0f, 0.0f), glm::vec3(0.0f, 0.1f, 0.0f));
	}

	GL_ERRORS();
}
