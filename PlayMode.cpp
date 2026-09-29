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

PlayMode::PlayMode(Client &client_) : client(client_), scene(*level_scene)
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
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size)
{

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
	}
	else if (evt.type == SDL_EVENT_MOUSE_MOTION)
	{
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == true)
		{
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y));
			controls.horiz += -motion.x * camera->fovy * mouse_sen;
			float d = -motion.x * camera->fovy * mouse_sen;
			spin_accum += std::abs(d);
			controls.vert += motion.y * camera->fovy * mouse_sen;
			controls.vert = glm::clamp(controls.vert, -1.4f, 1.4f);

			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed)
{

	// queue data for sending to server:
	controls.send_controls_message(&client.connection);

	// reset button press counters:
	controls.left.downs = 0;
	controls.right.downs = 0;
	controls.up.downs = 0;
	controls.down.downs = 0;
	controls.swat.downs = 0;

	// send/receive data:
	client.poll([this](Connection *c, Connection::Event event)
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
