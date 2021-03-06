#include "GameMode.hpp"

#include "MenuMode.hpp"
#include "Load.hpp"
#include "Sound.hpp"
#include "MeshBuffer.hpp"
#include "Scene.hpp"
#include "gl_errors.hpp" //helper for dumpping OpenGL error messages
#include "read_chunk.hpp" //helper for reading a vector of structures from a file
#include "data_path.hpp" //helper to get paths relative to executable
#include "compile_program.hpp" //helper to compile opengl shader programs
#include "draw_text.hpp" //helper to... um.. draw text
#include "vertex_color_program.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <fstream>
#include <map>
#include <cstddef>
#include <random>
#include <queue>

#define DEBUG
#ifdef DEBUG
#define dbg_cout(...) std::cout << __VA_ARGS__ << std::endl;
#else
#define dbg_cout(...)
#endif


Load< MeshBuffer > meshes(LoadTagDefault, [](){
	return new MeshBuffer(data_path("wolf_in_sheeps_clothing.pnc"));
});

Load< GLuint > meshes_for_vertex_color_program(LoadTagDefault, [](){
	return new GLuint(meshes->make_vao_for_program(vertex_color_program->program));
});

Load< Sound::Sample > sheep_sound(LoadTagDefault, [](){
	return new Sound::Sample(data_path("sheep.wav"));
});

Load< Sound::Sample > cow_sound(LoadTagDefault, [](){
	return new Sound::Sample(data_path("cow.wav"));
});

Load< Sound::Sample > pig_sound(LoadTagDefault, [](){
	return new Sound::Sample(data_path("pig.wav"));
});

Load< Sound::Sample > pig_dead_sound(LoadTagDefault, [](){
	return new Sound::Sample(data_path("pig_dead.wav"));
});

Load< Sound::Sample > shotgun_sound(LoadTagDefault, [](){
	return new Sound::Sample(data_path("shotgun.wav"));
});

auto start_with = [](std::string& target, std::string to_match) -> bool {
    return target.find(to_match) == 0;
};

// new line
std::map< uint32_t, Scene::Object* > animal_list;
Scene::Transform *cow_transform = nullptr;
Scene::Transform *pig_transform = nullptr;
Scene::Transform *sheep_transform = nullptr;
Scene::Transform *wolf_transform = nullptr;
Scene::Transform *crosshair_transform = nullptr;
Scene *non_const_scene = nullptr;  // non-const scene pointer for delete_transform function
//std::queue< std::pair< GLuint, GLuint > > animal_skin;
std::queue< std::pair< std::string, std::pair< GLuint, GLuint > > > animal_skin;

Scene::Camera *camera = nullptr;

Load< Scene > scene(LoadTagDefault, [](){
	Scene *ret = new Scene;
	//load transform hierarchy:
	ret->load(data_path("wolf_in_sheeps_clothing.scene"), [](Scene &s, Scene::Transform *t, std::string const &m){
		Scene::Object *obj = s.new_object(t);

		obj->program = vertex_color_program->program;
		obj->program_mvp_mat4  = vertex_color_program->object_to_clip_mat4;
		obj->program_mv_mat4x3 = vertex_color_program->object_to_light_mat4x3;
		obj->program_itmv_mat3 = vertex_color_program->normal_to_light_mat3;

		MeshBuffer::Mesh const &mesh = meshes->lookup(m);
		obj->vao = *meshes_for_vertex_color_program;
		obj->start = mesh.start;
		obj->count = mesh.count;

        if (m == "Cow" || m == "Pig" || m == "Sheep") {
            animal_skin.push(std::make_pair(m, std::make_pair(mesh.start, mesh.count)));
            //animal_skin.push(std::make_pair(mesh.start, mesh.count));
        }

	});
    non_const_scene = ret;

	//look up paddle and ball transforms:
	for (Scene::Transform *t = ret->first_transform; t != nullptr; t = t->alloc_next) {
        // new line
		if (t->name == "Cow") {
			if (cow_transform) throw std::runtime_error("Multiple 'Cow' transforms in scene.");
			cow_transform = t;
		}
		if (t->name == "Pig") {
			if (pig_transform) throw std::runtime_error("Multiple 'Pig' transforms in scene.");
			pig_transform = t;
		}
		if (t->name == "Sheep") {
			if (sheep_transform) throw std::runtime_error("Multiple 'Sheep' transforms in scene.");
			sheep_transform = t;
		}
		if (t->name == "Wolf") {
			if (wolf_transform) throw std::runtime_error("Multiple 'Wolf' transforms in scene.");
			wolf_transform = t;
		}
		if (t->name == "Crosshair") {
			if (crosshair_transform) throw std::runtime_error("Multiple 'Crosshair' transforms in scene.");
            crosshair_transform = t;
		}
	}
	if (!cow_transform) throw std::runtime_error("No 'Cow' transform in scene.");
	if (!pig_transform) throw std::runtime_error("No 'Pig' transform in scene.");
	if (!sheep_transform) throw std::runtime_error("No 'Sheep' transform in scene.");
	if (!wolf_transform) throw std::runtime_error("No 'Wolf' transform in scene.");
	if (!crosshair_transform) throw std::runtime_error("No 'Crosshair' transform in scene.");

	//look up the camera:
	for (Scene::Camera *c = ret->first_camera; c != nullptr; c = c->alloc_next) {
		if (c->transform->name == "Camera") {
			if (camera) throw std::runtime_error("Multiple 'Camera' objects in scene.");
			camera = c;
		}
	}
	if (!camera) throw std::runtime_error("No 'Camera' camera in scene.");
	return ret;
});

GameMode::GameMode(Client &client_) : client(client_) {
	client.connection.send_raw("h", 1); //send a 'hello' to the server

    // new line
    state.cow.x = cow_transform->position.x;
    state.cow.y = cow_transform->position.y;
    state.pig.x = pig_transform->position.x;
    state.pig.y = pig_transform->position.y;
    state.sheep.x = sheep_transform->position.x;
    state.sheep.y = sheep_transform->position.y;
    state.wolf.x = wolf_transform->position.x;
    state.wolf.y = wolf_transform->position.y;
    state.crosshair.x = crosshair_transform->position.x;
    state.crosshair.y = crosshair_transform->position.y;

    {  // register animal to animal_list
        uint32_t id = 1;
        for (Scene::Object *obj = scene->first_object; obj != nullptr; obj = obj->alloc_next) {
            std::string name = obj->transform->name;
            if (start_with(name, "Cow") || start_with(name, "Pig") ||
                start_with(name, "Sheep") || start_with(name, "Wolf")) {
                obj->transform->id = id;
                animal_list[id++] = obj;
                state.living_animal.insert(id);
            }
        }

        dbg_cout("Register animal_list");
        for (auto &it : animal_list) {
            dbg_cout("id " << it.first << " name " << it.second->transform->name);
        }
        dbg_cout("");
    }

}

GameMode::~GameMode() {
}

bool GameMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	//ignore any keys that are the result of automatic key repeat:
	if (evt.type == SDL_KEYDOWN && evt.key.repeat) {
		return false;
	}

	if (evt.type == SDL_MOUSEMOTION) {
		state.paddle.x = (evt.motion.x - 0.5f * window_size.x) / (0.5f * window_size.x) * Game::FrameWidth;
		state.paddle.x = std::max(state.paddle.x, -0.5f * Game::FrameWidth + 0.5f * Game::PaddleWidth);
		state.paddle.x = std::min(state.paddle.x,  0.5f * Game::FrameWidth - 0.5f * Game::PaddleWidth);
	}

    {  // player controls
        if (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP) {
            // both WASD and arrow keys can control crosshair/wolf
            if (evt.key.keysym.scancode == SDL_SCANCODE_UP ||
                evt.key.keysym.scancode == SDL_SCANCODE_W) {
                state.controls.move_up = (evt.type == SDL_KEYDOWN);
                return true;
            } else if (evt.key.keysym.scancode == SDL_SCANCODE_DOWN ||
                       evt.key.keysym.scancode == SDL_SCANCODE_S) {
                state.controls.move_down = (evt.type == SDL_KEYDOWN);
                return true;
            } else if (evt.key.keysym.scancode == SDL_SCANCODE_LEFT ||
                       evt.key.keysym.scancode == SDL_SCANCODE_A) {
                state.controls.move_left = (evt.type == SDL_KEYDOWN);
                return true;
            } else if (evt.key.keysym.scancode == SDL_SCANCODE_RIGHT ||
                       evt.key.keysym.scancode == SDL_SCANCODE_D) {
                state.controls.move_right = (evt.type == SDL_KEYDOWN);
                return true;
            }

            // press space to attack
            if (evt.key.keysym.scancode == SDL_SCANCODE_SPACE && evt.type == SDL_KEYDOWN) {
                if (state.identity.is_hunter) {
                    dbg_cout("Hunter attack");
                    shotgun_sound->play( crosshair_transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) );
                    {  // tell wolf to play shotgun
                        if (client.connection) {
                            client.connection.send_raw("s", 1);
                        }
                    }
                    for (auto &a : animal_list) {
                        uint32_t id = a.first;
                        Scene::Object *obj = a.second;
                        Scene::Transform *t = obj->transform;
                        if (glm::distance(state.crosshair, glm::vec2(t->position.x, t->position.y)) < 1.0f) {
                            dbg_cout("Try to kill id " << id << " name "<< t->name);
                            state.try_attack = std::make_pair(true, id);
                            break;
                        }
                    }
                } else if (state.identity.is_wolf) {
                    dbg_cout("Wolf attack");
                    for (auto &a : animal_list) {
                        uint32_t id = a.first;
                        Scene::Object *obj = a.second;
                        Scene::Transform *t = obj->transform;
                        if (t->name == "Wolf") {  // don't check wolf itself
                            continue;
                        }
                        if (glm::distance(state.wolf, glm::vec2(t->position.x, t->position.y)) < 1.0f) {
                            dbg_cout("Try to kill id " << id << " name "<< t->name);
                            state.try_attack = std::make_pair(true, id);
                            break;
                        }
                    }  // end for
                }  // end else if
            }  // end if

            // wolf press c to change skin
            if (evt.key.keysym.scancode == SDL_SCANCODE_C && evt.type == SDL_KEYDOWN) {
                if (client.connection && state.identity.is_wolf) {
                    client.connection.send_raw("c", 1);

                    auto skin = animal_skin.front();
                    animal_skin.pop();
                    animal_skin.push(skin);
                    if (skin.first == "Sheep") {
                        sheep_sound->play( wolf_transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) );
                    } else if (skin.first == "Cow") {
                        cow_sound->play( wolf_transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) );
                    } else if (skin.first == "Pig") {
                        pig_sound->play( wolf_transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) );
                    }
                }
            }
        }
    }


	return false;
}

void GameMode::update(float elapsed) {
	state.update(elapsed);

    // change wolf direction
    if (state.identity.is_wolf) {
        if (state.controls.move_up && state.controls.move_right) {
            wolf_transform->rotation = scene->direction.up_right;
            wolf_transform->direction = 2;
        } else if (state.controls.move_up && state.controls.move_left) {
            wolf_transform->rotation = scene->direction.up_left;
            wolf_transform->direction = 4;
        } else if (state.controls.move_down && state.controls.move_right) {
            wolf_transform->rotation = scene->direction.down_right;
            wolf_transform->direction = 8;
        } else if (state.controls.move_down && state.controls.move_left) {
            wolf_transform->rotation = scene->direction.down_left;
            wolf_transform->direction = 6;
        } else if (state.controls.move_up) {
            wolf_transform->rotation = scene->direction.up;
            wolf_transform->direction = 3;
        } else if (state.controls.move_down) {
            wolf_transform->rotation = scene->direction.down;
            wolf_transform->direction = 7;
        } else if (state.controls.move_right) {
            wolf_transform->rotation = scene->direction.right;
            wolf_transform->direction = 1;
        } else if (state.controls.move_left) {
            wolf_transform->rotation = scene->direction.left;
            wolf_transform->direction = 5;
        }
    }


    // send crosshair/wolf position to server when game state is changed
	if (client.connection && state.need_to_send()) {
        // positions
        if (state.identity.is_hunter) {
            // pc: Crosshair Position
            client.connection.send_raw("pc", 2);
            client.connection.send_raw(&state.crosshair.x, sizeof(float));
            client.connection.send_raw(&state.crosshair.y, sizeof(float));
        } else if (state.identity.is_wolf) {
            // pw: Wolf Position
            client.connection.send_raw("pw", 2);
            client.connection.send_raw(&state.wolf.x, sizeof(float));
            client.connection.send_raw(&state.wolf.y, sizeof(float));
        } else {
            std::cout << "no charactor" << std::endl;
        }

        // attack
        if (state.try_attack.first) {
            dbg_cout("send attack target id " << state.try_attack.second);
            client.connection.send_raw("a", 1);
            client.connection.send_raw(&state.try_attack.second, sizeof(uint32_t));
            state.try_attack = std::make_pair(false, 0);  // reset try_attack
        }

        // send direction data: ["d"][id][uint32_t direction]
        if (state.identity.is_wolf) {
            client.connection.send_raw("d", 1);
            client.connection.send_raw(&wolf_transform->id, sizeof(uint32_t));
            client.connection.send_raw(&wolf_transform->direction, sizeof(uint32_t));
        }
	}

	client.poll([&](Connection *c, Connection::Event event) {
		if (event == Connection::OnOpen) {
			//probably won't get this.
		} else if (event == Connection::OnClose) {
			std::cerr << "Lost connection to server." << std::endl;
		} else { assert(event == Connection::OnRecv);
            while (!c->recv_buffer.empty()) {
                // handle identity
                if (!state.identity.is_hunter && !state.identity.is_wolf) {
                    if (c->recv_buffer[0] == 'i') {  // "ih" or "iw"
                        if (c->recv_buffer.size() < 2) {
                            return;
                        } else {
                            if (c->recv_buffer[1] == 'h') {
                                state.identity.is_hunter = true;
                                // sent the size of animals
                                if (client.connection) {
                                    size_t size = state.living_animal.size();
                                    client.connection.send_raw("l", 1);
                                    client.connection.send_raw(&size, sizeof(size_t));
                                }
                            } else if (c->recv_buffer[1] == 'w') {
                                state.identity.is_wolf = true;
                            }
                            c->recv_buffer.clear();
                        }
                    } else if (*(c->recv_buffer.begin()) == 'p' && c->recv_buffer.size() >= 10) {
                        // ignore any data received before both of two players are registered

                        // probably won't get here because server will not send position data until both
                        // players are registered
                        c->recv_buffer.erase(c->recv_buffer.begin(),
                                             c->recv_buffer.begin() + 10);
                    } else if (*(c->recv_buffer.begin()) == 'a' && c->recv_buffer.size() >= 5) {
                        c->recv_buffer.erase(c->recv_buffer.begin(),
                                             c->recv_buffer.begin() + 5);
                    }
                // handle position update
                } else if (*(c->recv_buffer.begin()) == 'p') {  // [pw/pc][float x][float y]
                    if (c->recv_buffer.size() < 2 * (sizeof(char) + sizeof(float))) {
                        return;
                    } else {
                        if (*(c->recv_buffer.begin() + 1) == 'w' && state.identity.is_hunter) {
                            memcpy(&state.wolf.x, c->recv_buffer.data() + 2, sizeof(float));
                            memcpy(&state.wolf.y, c->recv_buffer.data() + 2 + sizeof(float), sizeof(float));
                        } else if (*(c->recv_buffer.begin() + 1) == 'c' && state.identity.is_wolf) {
                            memcpy(&state.crosshair.x, c->recv_buffer.data() + 2, sizeof(float));
                            memcpy(&state.crosshair.y, c->recv_buffer.data() + 2 + sizeof(float), sizeof(float));
                        }
                        c->recv_buffer.erase(c->recv_buffer.begin(),
                                             c->recv_buffer.begin() + 2 * (sizeof(char) + sizeof(float)));
                    }
                // handle attack event
                } else if (*(c->recv_buffer.begin()) == 'a') {
                    if (c->recv_buffer.size() < 1 + sizeof(uint32_t)) {
                        return;
                    } else {
                        uint32_t target;
                        memcpy(&target, c->recv_buffer.data() + 1, sizeof(uint32_t));
                        // remove from animal_list, scene, living_animal
                        if (!animal_list.empty()) {
                            Scene::Object *obj = animal_list[target];
                            dbg_cout("Receive kill id " << target << " name " << obj->transform->name);
                            if (start_with(obj->transform->name, "Pig")) {
                                pig_dead_sound->play(obj->transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                            }
                            non_const_scene->delete_object(obj);
                            animal_list.erase(target);
                        }
                        if (!state.living_animal.empty()) {
                            state.living_animal.erase(target);
                        }
                        c->recv_buffer.erase(c->recv_buffer.begin(),
                                             c->recv_buffer.begin() + 1 + sizeof(uint32_t));
                    }
                } else if (*(c->recv_buffer.begin()) == 'd') {
                    if (c->recv_buffer.size() < 1 + 2 * sizeof(uint32_t)) {
                        return;
                    } else {
                        if (state.identity.is_hunter) {
                            uint32_t id, direction;
                            memcpy(&id, c->recv_buffer.data() + 1, sizeof(uint32_t));
                            memcpy(&direction, c->recv_buffer.data() + 1 + sizeof(uint32_t), sizeof(uint32_t));
                            animal_list[id]->transform->rotation = *(scene->direction.direction_map.at(direction));
                        }
                        c->recv_buffer.erase(c->recv_buffer.begin(),
                                             c->recv_buffer.begin() + 1 + 2 * sizeof(uint32_t));
                    }
                } else if (*(c->recv_buffer.begin()) == 'c' && state.identity.is_hunter) {
                    // change wolf's skin
                    Scene::Object *obj = animal_list[wolf_transform->id];

                    auto skin = animal_skin.front();
                    animal_skin.pop();
                    obj->start = skin.second.first;
                    obj->count = skin.second.second;
                    animal_skin.push(skin);
                    if (skin.first == "Sheep") {
                        sheep_sound->play(wolf_transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                    } else if (skin.first == "Cow") {
                        cow_sound->play(wolf_transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), 2.0f);
                    } else if (skin.first == "Pig") {
                        pig_sound->play(wolf_transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                    }

                    c->recv_buffer.erase(c->recv_buffer.begin(),
                                         c->recv_buffer.begin() + 1);
                } else if (*(c->recv_buffer.begin()) == 's' && state.identity.is_wolf) {
                    shotgun_sound->play( crosshair_transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) );
                    c->recv_buffer.erase(c->recv_buffer.begin(),
                                         c->recv_buffer.begin() + 1);
                }
            }
		}

	});


	//copy game state to scene positions:
    crosshair_transform->position.x = state.crosshair.x;
    crosshair_transform->position.y = state.crosshair.y;

    wolf_transform->position.x = state.wolf.x;
    wolf_transform->position.y = state.wolf.y;

}

void GameMode::draw(glm::uvec2 const &drawable_size) {
	camera->aspect = drawable_size.x / float(drawable_size.y);

	//glClearColor(0.25f, 0.0f, 0.5f, 0.0f);
    glClearColor(136.0f/255.0f, 176.0f/255.0f, 75.0f/255.0f, 0.0);  // grass color
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	//set up basic OpenGL state:
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	//set up light positions:
	glUseProgram(vertex_color_program->program);

	glUniform3fv(vertex_color_program->sun_color_vec3, 1, glm::value_ptr(glm::vec3(0.81f, 0.81f, 0.76f)));
	glUniform3fv(vertex_color_program->sun_direction_vec3, 1, glm::value_ptr(glm::normalize(glm::vec3(-0.2f, 0.2f, 1.0f))));
	glUniform3fv(vertex_color_program->sky_color_vec3, 1, glm::value_ptr(glm::vec3(0.2f, 0.2f, 0.3f)));
	glUniform3fv(vertex_color_program->sky_direction_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 1.0f, 0.0f)));

	scene->draw(camera);

	GL_ERRORS();
}
