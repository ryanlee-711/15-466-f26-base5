#include "PlayMode.hpp"

#include "DrawLines.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <random>
#include <array>

GLuint level_meshes_for_lit_color_texture_program = 0;
// Load< MeshBuffer > level_meshes(LoadTagDefault, []() -> MeshBuffer const * {
// 	MeshBuffer const *ret = new MeshBuffer(data_path("level.pnct"));
// 	level_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
// 	return ret;
// });

// Load< Scene > level_scene(LoadTagDefault, []() -> Scene const * {
// 	return new Scene(data_path("level.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
// 		Mesh const &mesh = level_meshes->lookup(mesh_name);

// 		scene.drawables.emplace_back(transform);
// 		Scene::Drawable &drawable = scene.drawables.back();

// 		drawable.pipeline = lit_color_texture_program_pipeline;

// 		drawable.pipeline.vao = level_meshes_for_lit_color_texture_program;
// 		drawable.pipeline.type = mesh.type;
// 		drawable.pipeline.start = mesh.start;
// 		drawable.pipeline.count = mesh.count;

// 	});
// });

// PlayMode::PlayMode() : scene(*level_scene) {
// 	std::unordered_map<Scene::Transform*, Scene::Drawable*> transform_to_drawable;
// 	for (auto &d : scene.drawables) {
// 		transform_to_drawable[d.transform] = &d;
// 	}

// 	for (auto &transform : scene.transforms) {
// 		if (transform.name == "Dragon") dragon = &transform;
// 		if (transform.name.starts_with("Pillar")) {
// 			Pillar pil;
// 			pil.position = transform.position;
// 			pil.radius = 1.14f * std::max(std::abs(transform.scale.x), std::abs(transform.scale.y));
// 			pil.height = transform.position.z + (0.5f * 2.0f * std::abs(transform.scale.z));
// 			pillars.emplace_back(pil);
// 		}
// 		if (transform.name.starts_with("Egg")) {
// 			Egg egg;
// 			egg.transform = &transform;
// 			auto drawb = transform_to_drawable.find(&transform);
// 			if (drawb != transform_to_drawable.end()) egg.drawable = drawb->second;
// 			eggs.emplace_back(egg);
// 		}
// 		if (transform.name == "WingL") wingL = &transform;
// 		if (transform.name == "WingR") wingR = &transform;
// 	}
// 	if (dragon == nullptr) throw std::runtime_error("Dragon not found.");
// 	if (wingL == nullptr) throw std::runtime_error("WingL not found.");
// 	if (wingR == nullptr) throw std::runtime_error("WingR not found.");
// 	wingL_base = wingL->rotation;
// 	wingR_base = wingR->rotation;

// 	//get pointer to camera for convenience:
// 	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
// 	camera = &scene.cameras.front();
// }

PlayMode::PlayMode(Client &client_) : client(client_) {
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.repeat) {
			//ignore repeats
		} else if (evt.key.key == SDLK_A) {
			controls.left.downs += 1;
			controls.left.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_D) {
			controls.right.downs += 1;
			controls.right.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_W) {
			controls.up.downs += 1;
			controls.up.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_S) {
			controls.down.downs += 1;
			controls.down.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			controls.swat.downs += 1;
			controls.swat.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_A) {
			controls.left.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_D) {
			controls.right.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_W) {
			controls.up.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_S) {
			controls.down.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			controls.swat.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == true) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
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

void PlayMode::update(float elapsed) {

	//queue data for sending to server:
	controls.send_controls_message(&client.connection);

	//reset button press counters:
	controls.left.downs = 0;
	controls.right.downs = 0;
	controls.up.downs = 0;
	controls.down.downs = 0;
	controls.swat.downs = 0;

	//send/receive data:
	client.poll([this](Connection *c, Connection::Event event){
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
		}
	}, 0.0);
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {

	static std::array< glm::vec2, 16 > const circle = [](){
		std::array< glm::vec2, 16 > ret;
		for (uint32_t a = 0; a < ret.size(); ++a) {
			float ang = a / float(ret.size()) * 2.0f * float(M_PI);
			ret[a] = glm::vec2(std::cos(ang), std::sin(ang));
		}
		return ret;
	}();

	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	//figure out view transform to center the arena:
	float aspect = float(drawable_size.x) / float(drawable_size.y);
	float scale = std::min(
		2.0f * aspect / (Game::ArenaMax.x - Game::ArenaMin.x + 2.0f * Game::HumanRadius),
		2.0f / (Game::ArenaMax.y - Game::ArenaMin.y + 2.0f * Game::HumanRadius)
	);
	glm::vec2 offset = -0.5f * (Game::ArenaMax + Game::ArenaMin);

	glm::mat4 world_to_clip = glm::mat4(
		scale / aspect, 0.0f, 0.0f, offset.x,
		0.0f, scale, 0.0f, offset.y,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);

	// {
	// 	DrawLines lines(world_to_clip);

	// 	//helper:
	// 	auto draw_text = [&](glm::vec2 const &at, std::string const &text, float H) {
	// 		lines.draw_text(text,
	// 			glm::vec3(at.x, at.y, 0.0),
	// 			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
	// 			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
	// 		float ofs = (1.0f / scale) / drawable_size.y;
	// 		lines.draw_text(text,
	// 			glm::vec3(at.x + ofs, at.y + ofs, 0.0),
	// 			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
	// 			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	// 	};

	// 	lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMin.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
	// 	lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMax.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
	// 	lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMin.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
	// 	lines.draw(glm::vec3(Game::ArenaMax.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));

	// 	for (auto const &player : game.players) {
	// 		glm::u8vec4 col = glm::u8vec4(player.color.x*255, player.color.y*255, player.color.z*255, 0xff);
	// 		if (&player == &game.players.front()) {
	// 			//mark current player (which server sends first):
	// 			lines.draw(
	// 				glm::vec3(player.position + Game::HumanRadius * glm::vec3(-0.5f,-0.5f, -0.5f), 0.0f),
	// 				glm::vec3(player.position + Game::HumanRadius * glm::vec3( 0.5f, 0.5f, 0.5f), 0.0f),
	// 				col
	// 			);
	// 			lines.draw(
	// 				glm::vec3(player.position + Game::HumanRadius * glm::vec3(-0.5f, 0.5f, 0.5f), 0.0f),
	// 				glm::vec3(player.position + Game::HumanRadius * glm::vec3( 0.5f,-0.5f, 0.5f), 0.0f),
	// 				col
	// 			);
	// 		}
	// 		for (uint32_t a = 0; a < circle.size(); ++a) {
	// 			lines.draw(
	// 				glm::vec3(player.position + Game::HumanRadius * circle[a], 0.0f, 0.0f),
	// 				glm::vec3(player.position + Game::HumanRadius * circle[(a+1)%circle.size()], 0.0f, 0.0f),
	// 				col
	// 			);
	// 		}

	// 		draw_text(player.position + glm::vec3(0.0f, -0.1f + Game::HumanRadius, 0.0f), player.name, 0.09f);
	// 	}
	// }
	GL_ERRORS();
}
