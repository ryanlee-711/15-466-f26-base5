#include "PlayMode.hpp"

#include "DrawLines.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"
#include "Mesh.hpp"
#include "LitColorTextureProgram.hpp"

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <ifaddrs.h>
#include <arpa/inet.h>
#include <random>
#include <array>

GLuint level_meshes_for_lit_color_texture_program = 0;
Load<MeshBuffer> level_meshes(LoadTagDefault, []() -> MeshBuffer const *
							  {
	MeshBuffer const *ret = new MeshBuffer(data_path("room1.pnct"));
	level_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret; });

Load<Scene> level_scene(LoadTagDefault, []() -> Scene const *
						{ return new Scene(data_path("room1.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name)
										   {
											   Mesh const &mesh = level_meshes->lookup(mesh_name);

											   scene.drawables.emplace_back(transform);
											   Scene::Drawable &drawable = scene.drawables.back();

											   drawable.pipeline = lit_color_texture_program_pipeline;

											   drawable.pipeline.vao = level_meshes_for_lit_color_texture_program;
											   drawable.pipeline.type = mesh.type;
											   drawable.pipeline.start = mesh.start;
											   drawable.pipeline.count = mesh.count; }); });

static std::string my_ip()
{
	std::string ret;
	ifaddrs *addrs;
	getifaddrs(&addrs);
	for (ifaddrs *a = addrs; a; a = a->ifa_next)
	{
		if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET)
		{
			std::string s = inet_ntoa(((sockaddr_in *)a->ifa_addr)->sin_addr);
			if (!s.starts_with("127.")) ret = s;
		}
	}
	freeifaddrs(addrs);
	return ret;
}

PlayMode::PlayMode() : scene(*level_scene)
{
	for (auto &transform : scene.transforms)
	{
		// if (transform.name.starts_with("FlyInstance")) flies.push_back(&transform);
		// if (transform.name.starts_with("HumanInstance")) humans.push_back(&transform);
	}
	camera = &scene.cameras.front();
}

PlayMode::~PlayMode()
{
	if (server) SDL_KillProcess(server, true);
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size)
{
	if (!game.started && evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
	{
		int row = int(10.0f * evt.button.y / float(window_size.y));
		if (screen == Menu && row == 4) screen = Join;
		else if (screen == Menu && row == 5)
		{
			std::string path = data_path("server");
			char const *args[] = {path.c_str(), "1337", nullptr};
			server = SDL_CreateProcess(args, false);
			ip = my_ip();
			screen = Lobby;
		}
		else if (screen == Menu && row == 6) Mode::set_current(nullptr);
		else if (screen == Lobby && row == 4) controls.fly.downs += 1;
		else if (screen == Lobby && row == 5) controls.human.downs += 1;
		else if (screen == Lobby && row == 6) controls.start.downs += 1;
		return true;
	}
	if (screen == Join && evt.type == SDL_EVENT_KEY_DOWN)
	{
		if (evt.key.key == SDLK_BACKSPACE && !ip.empty()) ip.pop_back();
		else if (evt.key.key == SDLK_PERIOD || (evt.key.key >= SDLK_0 && evt.key.key <= SDLK_9)) ip += char(evt.key.key);
		else if (evt.key.key == SDLK_RETURN)
		{
			try
			{
				client = new Client(ip, "1337");
				screen = Lobby;
			}
			catch (std::exception const &) {}
		}
		return true;
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
			controls.swat.downs += 1;
			controls.swat.pressed = true;
			return true;
		}
		else if (evt.key.key == SDLK_ESCAPE) {
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
			controls.swat.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == false) {
			SDL_SetWindowRelativeMouseMode(Mode::window, true);
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_MOTION)
	{
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == true)
		{
			// glm::vec2 motion = glm::vec2(
			// 	evt.motion.xrel / float(window_size.y),
			// 	-evt.motion.yrel / float(window_size.y));
			// controls.horiz += -motion.x * camera->fovy * mouse_sen;
			// float d = -motion.x * camera->fovy * mouse_sen;
			// spin_accum += std::abs(d);
			// controls.vert += motion.y * camera->fovy * mouse_sen;
			// controls.vert = glm::clamp(controls.vert, -1.4f, 1.4f);

			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed)
{
	if (screen == Lobby && client == nullptr)
	{
		try
		{
			client = new Client("localhost", "1337");
		}
		catch (std::exception const &) {}
	}
	if (client == nullptr) return;

	// queue data for sending to server:
	controls.send_controls_message(&client->connection);

	// reset button press counters:
	controls.left.downs = 0;
	controls.right.downs = 0;
	controls.up.downs = 0;
	controls.down.downs = 0;
	controls.swat.downs = 0;
	controls.fly.downs = 0;
	controls.human.downs = 0;
	controls.start.downs = 0;

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

	if (!game.started) return;

	for (auto t : flies)
		t->scale = glm::vec3(0.0f);
	for (auto t : humans)
		t->scale = glm::vec3(0.0f);
	uint32_t f = 0, h = 0;
	for (auto const &p : game.players)
	{
		Scene::Transform *t;
		if (p.role == Role::Human)
		{
			t = humans[h++];
			t->rotation = glm::angleAxis(p.horiz, glm::vec3(0.0f, 0.0f, 1.0f));
		}
		else
		{
			t = flies[f++];
			t->rotation = glm::angleAxis(p.horiz, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::angleAxis(p.vert, glm::vec3(1.0f, 0.0f, 0.0f)) * glm::angleAxis(p.turnDip, glm::vec3(0.0f, 1.0f, 0.0f));
		}
		t->position = p.position;
		if (p.alive)
			t->scale = glm::vec3(1.0f);
	}
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
			if (server && game.count(Role::Fly) >= 1 && game.count(Role::Human) >= 1) row(6, "Start Game");
		}
		GL_ERRORS();
		return;
	}

	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

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

	scene.draw(*camera);

	GL_ERRORS();
}
