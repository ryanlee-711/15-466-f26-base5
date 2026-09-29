#include "Game.hpp"

#include "Connection.hpp"
#include "Scene.hpp"
#include "data_path.hpp"

#include <glm/gtc/quaternion.hpp>
#include <stdexcept>
#include <iostream>
#include <cstring>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

void Player::Controls::send_controls_message(Connection *connection_) const
{
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = 17;
	connection.send(Message::C2S_Controls);
	connection.send(uint8_t(size));
	connection.send(uint8_t(size >> 8));
	connection.send(uint8_t(size >> 16));

	auto send_button = [&](Button const &b)
	{
		if (b.downs & 0x80)
		{
			std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
		}
		connection.send(uint8_t((b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f)));
	};

	send_button(left);
	send_button(right);
	send_button(up);
	send_button(down);
	send_button(space);
	send_button(shift);
	send_button(fly);
	send_button(human);
	send_button(start);
	connection.send(horiz);
	connection.send(vert);
}

bool Player::Controls::recv_controls_message(Connection *connection_)
{
	assert(connection_);
	auto &connection = *connection_;

	auto &recv_buffer = connection.recv_buffer;

	// expecting [type, size_low0, size_mid8, size_high8]:
	if (recv_buffer.size() < 4)
		return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_Controls))
		return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16) | (uint32_t(recv_buffer[2]) << 8) | uint32_t(recv_buffer[1]);
	if (size != 17)
		throw std::runtime_error("Controls message with size " + std::to_string(size) + " != 16!");

	// expecting complete message:
	if (recv_buffer.size() < 4 + size)
		return false;

	auto recv_button = [](uint8_t byte, Button *button)
	{
		button->pressed = (byte & 0x80);
		uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
		if (d > 255)
		{
			std::cerr << "got a whole lot of downs" << std::endl;
			d = 255;
		}
		button->downs = uint8_t(d);
	};

	recv_button(recv_buffer[4 + 0], &left);
	recv_button(recv_buffer[4 + 1], &right);
	recv_button(recv_buffer[4 + 2], &up);
	recv_button(recv_buffer[4 + 3], &down);
	recv_button(recv_buffer[4 + 4], &space);
	recv_button(recv_buffer[4 + 5], &shift);
	recv_button(recv_buffer[4 + 6], &fly);
	recv_button(recv_buffer[4 + 7], &human);
	recv_button(recv_buffer[4 + 8], &start);
	std::memcpy(&horiz, &recv_buffer[4 + 9], sizeof(float));
	std::memcpy(&vert, &recv_buffer[4 + 13], sizeof(float));

	// delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}

Game::Game() : mt(0x15466666)
{
	Scene collision(data_path("collision.scene"), nullptr);
	for (auto const &t : collision.transforms)
	{
		glm::vec3 half_extents = glm::abs(t.scale);
		boxes.push_back(Box{t.position - half_extents, t.position + half_extents});
	}
}

bool Game::overlaps_collision_box(Player const &player, glm::vec3 const &position) const
{
	float radius = player.role == Role::Human ? HumanRadius : FlyRadius;
	glm::vec3 player_min = position - glm::vec3(radius);
	glm::vec3 player_max = position + glm::vec3(radius);
	if (player.role == Role::Human)
	{
		player_min.z = position.z;
		player_max.z = position.z + HumanHeight;
	}

	for (auto const &box : boxes)
	{
		if (player_max.x <= box.min.x || player_min.x >= box.max.x)
			continue;
		if (player_max.y <= box.min.y || player_min.y >= box.max.y)
			continue;
		if (player_max.z <= box.min.z || player_min.z >= box.max.z)
			continue;
		return true;
	}
	return false;
}

Player *Game::spawn_player()
{
	players.emplace_back();
	Player &player = players.back();
	bool isHuman = next_player_number % 2 == 0;

	if (isHuman)
	{
		// random point in the middle area of the arena: FIX ONCE MAP IS MADE
		player.position.x = glm::mix(ArenaMin.x + 2.0f * HumanRadius, ArenaMax.x - 2.0f * HumanRadius, 0.4f + 0.2f * mt() / float(mt.max()));
		player.position.y = glm::mix(ArenaMin.y + 2.0f * HumanRadius, ArenaMax.y - 2.0f * HumanRadius, 0.4f + 0.2f * mt() / float(mt.max()));
		player.position.z = ArenaMin.z;
		player.role = Role::Human;
	}
	else
	{
		player.position.x = glm::mix(ArenaMin.x + 2.0f * FlyRadius, ArenaMax.x - 2.0f * FlyRadius, 0.4f + 0.2f * mt() / float(mt.max()));
		player.position.y = glm::mix(ArenaMin.y + 2.0f * FlyRadius, ArenaMax.y - 2.0f * FlyRadius, 0.4f + 0.2f * mt() / float(mt.max()));
		player.position.z = 1.2f;
		player.role = Role::Fly;
		flies_remaining++;
	}

	do
	{
		player.color.r = mt() / float(mt.max());
		player.color.g = mt() / float(mt.max());
		player.color.b = mt() / float(mt.max());
	} while (player.color == glm::vec3(0.0f));
	player.color = glm::normalize(player.color);

	player.id = next_player_number;
	player.name = "Player " + std::to_string(next_player_number++);

	return &player;
}

void Game::remove_player(Player *player)
{
	bool found = false;
	for (auto pi = players.begin(); pi != players.end(); ++pi)
	{
		if (&*pi == player)
		{
			if (pi->role == Role::Fly && pi->alive)
			{
				flies_remaining--;
				if (flies_remaining == 0)
				{
					humanWon = true;
				}
			}
			players.erase(pi);
			found = true;
			break;
		}
	}
	assert(found);
}

uint32_t Game::count(Role role) const
{
	uint32_t n = 0;
	for (auto const &p : players)
	{
		if (p.role == role)
			n++;
	}
	return n;
}

void Game::update(float elapsed)
{
	if (!started)
	{
		for (auto &p : players)
		{
			if (p.controls.fly.downs && count(Role::Fly) < 3)
				p.role = Role::Fly;
			if (p.controls.human.downs && count(Role::Human) < 3)
				p.role = Role::Human;
			if (p.controls.start.downs && &p == &players.front() && count(Role::Fly) >= 1 && count(Role::Human) >= 1)
			{
				started = true;
				flies_remaining = count(Role::Fly);
			}
			p.controls.fly.downs = 0;
			p.controls.human.downs = 0;
			p.controls.start.downs = 0;
		}
		return;
	}
	for (auto &p : players)
	{
		p.swat_time = std::max(0.0f, p.swat_time - elapsed);
		p.swat_cooldown = std::max(0.0f, p.swat_cooldown - elapsed);
		if (p.role != Role::Human)
			continue;
		if (!p.controls.space.downs || p.swat_cooldown > 0.0f)
			continue;
		p.swat_time = SwatDuration;
		p.swat_cooldown = SwatCooldown;
		glm::quat yaw = glm::angleAxis(p.horiz, glm::vec3(0.0f, 0.0f, 1.0f));
		glm::quat look = yaw * glm::angleAxis(p.vert, glm::vec3(1.0f, 0.0f, 0.0f));
		glm::vec3 eye = p.position + yaw * SwatEye;
		glm::vec3 forward = look * glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 right = look * glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec3 up = look * glm::vec3(0.0f, 0.0f, 1.0f);
		for (auto &f : players)
		{
			if (f.role != Role::Fly || !f.alive)
				continue;
			glm::vec3 d = f.position - eye;
			if (glm::dot(d, forward) < 0.0f || glm::dot(d, forward) > SwatReach)
				continue;
			if (std::abs(glm::dot(d, right)) > SwatHalfSize || std::abs(glm::dot(d, up)) > SwatHalfSize)
				continue;
			f.alive = false;
			f.dead_at = glm::vec3(f.position.x, f.position.y, FlyRadius);
			flies_remaining--;
			if (flies_remaining == 0)
				humanWon = true;
		}
	}
	// position/velocity update:
	for (auto &p : players)
	{
		p.previous_position = p.position;
		glm::vec3 dir = glm::vec3(0.0f, 0.0f, 0.0f);

		// First person movement for Humans
		if (p.role == Role::Human)
		{
			p.horiz = p.controls.horiz;
			p.vert = p.controls.vert;

			glm::vec2 move = glm::vec2(0.0f);
			if (p.controls.left.pressed)
				move.x -= 1.0f;
			if (p.controls.right.pressed)
				move.x += 1.0f;
			if (p.controls.down.pressed)
				move.y -= 1.0f;
			if (p.controls.up.pressed)
				move.y += 1.0f;

			if (move != glm::vec2(0.0f))
				move = glm::normalize(move);

			glm::vec3 forward = glm::vec3(-std::sin(p.horiz), std::cos(p.horiz), 0.0f);
			glm::vec3 right = glm::vec3(std::cos(p.horiz), std::sin(p.horiz), 0.0f);

			dir += move.x * right + move.y * forward;
			p.velocity.z = 0;
			p.position.z = ArenaMin.z;

			if (dir == glm::vec3(0.0f))
			{
				// no inputs: just drift to a stop
				float amt = 1.0f - std::pow(0.5f, elapsed / (HumanAccelHalflife * 2.0f));
				p.velocity = glm::mix(p.velocity, glm::vec3(0.0f), amt);
			}
			else
			{
				// inputs: tween velocity to target direction
				dir = glm::normalize(dir);

				float amt = 1.0f - std::pow(0.5f, elapsed / HumanAccelHalflife);

				// accelerate along velocity (if not fast enough):
				float along = glm::dot(p.velocity, dir);
				glm::vec3 perp = p.velocity - along * dir;
				if (along < HumanSpeed)
				{
					along = glm::mix(along, HumanSpeed, amt);
				}

				// damp perpendicular velocity:
				perp = glm::mix(perp, glm::vec3(0.0f), amt);
				p.velocity = along * dir + perp;
			}

			p.position += p.velocity * elapsed;
		}
		// Third Person movement for Flies
		else
		{
			float horizIn = 0.0f;
			float climb = 0.0f;
			if (p.controls.left.pressed)
				horizIn += 1.0f;
			if (p.controls.right.pressed)
				horizIn -= 1.0f;
			if (p.controls.space.pressed)
				climb += 1.0f;
			if (p.controls.shift.pressed)
				climb -= 1.0f;

			p.horiz += horizIn * HorizRate * elapsed;
			float amt = 1.0f - std::pow(0.5f, elapsed / (FlyAccelHalflife * 2.0f));
			p.vert = glm::mix(p.vert, climb * 0.4f, amt);
			p.vert = glm::clamp(p.vert, -1.0f, 1.0f);

			float next_speed = 0.0f;
			if (p.controls.up.pressed)
				next_speed = FlySpeed;
			if (p.controls.down.pressed)
				next_speed = -0.5f * FlySpeed;
			float rate = Accel;
			if (next_speed < p.speed)
				rate = Decel;
			p.speed += (next_speed - p.speed) * std::min(1.0f, rate * elapsed);

			float newTurnDip = -horizIn * 0.6f;
			p.turnDip += (newTurnDip - p.turnDip) * std::min(1.0f, 4.0f * elapsed);

			glm::vec3 forward = glm::vec3(-std::sin(p.horiz), std::cos(p.horiz), 0.0f);
			p.velocity = forward * p.speed + glm::vec3(0.0f, 0.0f, climb * FlyClimbSpeed);

			// Fly's target position
			p.position += p.velocity * elapsed;

			// Calculate offset from fly noise
			p.offset = FlyNoiseAmplitude * (glm::vec3(
																					glm::sin(time * FlyNoise1Freq.x),
																					glm::sin(time * FlyNoise1Freq.y),
																					glm::sin(time * FlyNoise1Freq.z)) +
																			glm::vec3(
																					glm::sin(time * FlyNoise2Freq.x),
																					glm::sin(time * FlyNoise2Freq.y),
																					glm::sin(time * FlyNoise2Freq.z)));
		}

		// reset 'downs' since controls have been handled:
		p.controls.left.downs = 0;
		p.controls.right.downs = 0;
		p.controls.up.downs = 0;
		p.controls.down.downs = 0;
		p.controls.space.downs = 0;
		p.controls.shift.downs = 0;
	}

	// collision resolution:
	for (auto &p1 : players)
	{
		// player/player collisions:
		float p1Rad;
		if (p1.role == Role::Human)
			p1Rad = HumanRadius;
		else
			p1Rad = FlyRadius;
		for (auto &p2 : players)
		{
			if (&p1 == &p2)
				break;
			if (p1.role != p2.role)
				continue;
			glm::vec3 p12 = p2.position - p1.position;
			float len2 = glm::length2(p12);
			float p2Rad;
			if (p2.role == Role::Human)
				p2Rad = HumanRadius;
			else
				p2Rad = FlyRadius;
			if (len2 > (p1Rad + p2Rad) * (p1Rad + p2Rad))
				continue;
			if (len2 == 0.0f)
				continue;
			glm::vec3 dir = p12 / std::sqrt(len2);
			// mirror velocity to be in separating direction:
			glm::vec3 v12 = p2.velocity - p1.velocity;
			glm::vec3 delta_v12 = dir * glm::max(0.0f, -1.75f * glm::dot(dir, v12));
			p2.velocity += 0.5f * delta_v12;
			p1.velocity -= 0.5f * delta_v12;
		}
		if (overlaps_collision_box(p1, p1.position))
		{
			p1.position = p1.previous_position;
			p1.velocity = glm::vec3(0.0f);
			p1.speed = 0.0f;
		}
	}

	for (auto &p : players)
	{
		if (!p.alive)
			p.position = p.dead_at;
	}
}

void Game::send_state_message(Connection *connection_, Player *connection_player) const
{
	assert(connection_);
	auto &connection = *connection_;

	connection.send(Message::S2C_State);
	// will patch message size in later, for now placeholder bytes:
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	size_t mark = connection.send_buffer.size(); // keep track of this position in the buffer

	// send player info helper:
	auto send_player = [&](Player const &player)
	{
		connection.send(player.role);
		connection.send(player.position);
		connection.send(player.velocity);
		connection.send(player.color);

		// NOTE: can't just 'send(name)' because player.name is not plain-old-data type.
		// effectively: truncates player name to 255 chars
		uint8_t len = uint8_t(std::min<size_t>(255, player.name.size()));
		connection.send(len);
		connection.send_buffer.insert(connection.send_buffer.end(), player.name.begin(), player.name.begin() + len);

		connection.send(player.id);
		connection.send(player.vert);
		connection.send(player.horiz);
		connection.send(player.speed);
		connection.send(player.turnDip);
		connection.send(player.alive);
		connection.send(player.swat_time);
	};

	// Game Variables:
	connection.send(flies_remaining);
	connection.send(time);
	connection.send(humanWon);
	connection.send(started);

	// player count:
	connection.send(uint8_t(players.size()));
	if (connection_player)
		send_player(*connection_player);
	for (auto const &player : players)
	{
		if (&player == connection_player)
			continue;
		send_player(player);
	}

	// compute the message size and patch into the message header:
	uint32_t size = uint32_t(connection.send_buffer.size() - mark);
	connection.send_buffer[mark - 3] = uint8_t(size);
	connection.send_buffer[mark - 2] = uint8_t(size >> 8);
	connection.send_buffer[mark - 1] = uint8_t(size >> 16);
}

bool Game::recv_state_message(Connection *connection_)
{
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	if (recv_buffer.size() < 4)
		return false;
	if (recv_buffer[0] != uint8_t(Message::S2C_State))
		return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16) | (uint32_t(recv_buffer[2]) << 8) | uint32_t(recv_buffer[1]);
	uint32_t at = 0;
	// expecting complete message:
	if (recv_buffer.size() < 4 + size)
		return false;

	// copy bytes from buffer and advance position:
	auto read = [&](auto *val)
	{
		if (at + sizeof(*val) > size)
		{
			throw std::runtime_error("Ran out of bytes reading state message.");
		}
		std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
		at += sizeof(*val);
	};

	read(&flies_remaining);
	read(&time);
	read(&humanWon);
	read(&started);

	players.clear();
	uint8_t player_count;
	read(&player_count);
	for (uint8_t i = 0; i < player_count; ++i)
	{
		players.emplace_back();
		Player &player = players.back();
		read(&player.role);
		read(&player.position);
		read(&player.velocity);
		read(&player.color);
		uint8_t name_len;
		read(&name_len);
		// n.b. would probably be more efficient to directly copy from recv_buffer, but I think this is clearer:
		player.name = "";
		for (uint8_t n = 0; n < name_len; ++n)
		{
			char c;
			read(&c);
			player.name += c;
		}
		// Game specific reads
		read(&player.id);
		read(&player.vert);
		read(&player.horiz);
		read(&player.speed);
		read(&player.turnDip);
		read(&player.alive);
		read(&player.swat_time);
	}

	if (at != size)
		throw std::runtime_error("Trailing data in state message.");

	// delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}
