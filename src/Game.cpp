#include "Game.h"
#include "Qor/BasicPartitioner.h"
#include "Qor/Input.h"
#include "Qor/Qor.h"
#include "Qor/Shader.h"
#include <glm/glm.hpp>
#include <cstdlib>
#include <chrono>
#include <thread>
using namespace std;
using namespace glm;
namespace _ = std::placeholders;

Game :: Game(Qor* engine):
    m_pQor(engine),
    m_pResources(engine->resources()),
    m_pInput(engine->input()),
    m_pRoot(make_shared<Node>()),
    m_pPipeline(engine->pipeline()),
    m_pPartitioner(engine->pipeline()->partitioner()),
    m_pController(engine->session()->active_profile(0)->controller()),
    m_JumpTimer(engine->timer()->timeline())
{
}

void Game :: preload()
{
    float sw = m_pQor->window()->size().x;
    float sh = m_pQor->window()->size().y;

    m_pCamera = make_shared<Camera>(m_pQor->resources(), m_pQor->window());
    m_pConsole = make_shared<Console>(
        m_pQor->interpreter(),
        m_pQor->window(),
        m_pQor->input(),
        m_pQor->resources()
    );
    m_pRoot->add(m_pCamera);

    m_pMap = m_pQor->make<TileMap>("1.tmx");
    m_pRoot->add(m_pMap);

    m_pMusic = m_pQor->make<Sound>("thejungle.ogg");
    m_pRoot->add(m_pMusic);
    
    auto scale = 150.0f / std::max<float>(sw * 1.0f,1.0f);
    m_pCamera->rescale(glm::vec3(
        scale, scale,
        1.0f
    ));

    m_pChar = m_pQor->make<Sprite>("guy.json");
    m_pRoot->add(m_pChar);
    m_pChar->set_states({"stand","right"});
    m_pCamera->position(glm::vec3(-64.0f, -64.0f, 0.0f));
    m_pChar->position(glm::vec3(0.0f, 0.0f, 1.0f));

    m_pCharFocusRight = make_shared<Node>();
    m_pCharFocusRight->position(glm::vec3(32.0f, 0.0f, 0.0f));
    m_pCharFocusLeft = make_shared<Node>();
    m_pCharFocusLeft->position(glm::vec3(-32.0f, 0.0f, 0.0f));
    m_pChar->add(m_pCharFocusRight);
    m_pChar->add(m_pCharFocusLeft);
    
    m_pCamera->mode(Tracker::FOLLOW);
    m_pCamera->track(m_pCharFocusRight);
    m_pCamera->focus_time(Freq::Time::ms(200));
    m_pCamera->focal_offset(vec3(
        -m_pQor->window()->center().x * 1.0f,
        -m_pQor->window()->center().y * 1.2f,
        0.0f
    ));
    m_pCamera->listen(true);

    m_pViewLight = make_shared<Light>();
    m_pViewLight->ambient(Color::white() * 1.0f);
    m_pViewLight->diffuse(Color::black());
    m_pViewLight->specular(Color::black());
    m_pViewLight->dist(sw / 1.5f);
    m_pViewLight->position(glm::vec3(
        m_pQor->window()->center().x * 1.0f,
        m_pQor->window()->center().y * 1.2f,
        1.0f
    ));
    m_pCamera->add(m_pViewLight);

    vector<vector<shared_ptr<TileLayer>>*> layer_types {
        &m_pMap->layers(),
        &m_pMap->object_layers()
    };
    for(auto&& layers: layer_types)
    for(auto&& layer: *layers)
    {
        for(auto&& tile_ptr: layer->all_descendants())
        {
            if(not tile_ptr)
                continue;
            auto tile = tile_ptr->as_node();
            // read object properties and replace
            auto obj = std::dynamic_pointer_cast<MapTile>(tile);
            if(obj)
            {
                auto obj_cfg = obj->config();
                obj->box() = obj->mesh()->box();
                if(obj_cfg->at<string>("name","")=="player_start")
                {
                    obj->visible(false);
                    obj->mesh()->visible(false);
                    m_Spawns.push_back(obj.get());
                    continue;
                }
                else
                {
                    
                }
                bool depth = layer->depth() || obj_cfg->has("depth");
                bool fatal = obj_cfg->has("fatal");
                if(depth)
                {
                    auto n = make_shared<Node>();
                    n->name("mask");
                    auto mask = obj_cfg->at<shared_ptr<Meta>>("mask", shared_ptr<Meta>());
                    bool hflip = obj->orientation() & (unsigned)MapTile::Orientation::H;
                    bool vflip = obj->orientation() & (unsigned)MapTile::Orientation::V;
                    //if(obj_cfg->has("sidewall") && not mask)
                    //{
                    //    hflip ^= obj_cfg->at<string>("sidewall","")=="right";
                    //    mask = make_shared<Meta>();
                    //    mask->append<double>({0.0, 0.0, 0.25, 1.0});
                    //}
                    if(mask && mask->size()==4)
                    {
                        n->box() = Box(
                            vec3(mask->at<double>(0), mask->at<double>(1), K_EPSILON * 5.0f),
                            vec3(mask->at<double>(2), mask->at<double>(3), 0.5f)
                        );
                    }
                    else
                    {
                        n->box() = Box(
                            vec3(0.0f, 0.0f, -5.0f),
                            vec3(1.0f, 1.0f, 5.0f)
                        );
                    }
                    if(hflip){
                        n->box().min().x = 1.0f - n->box().min().x;
                        n->box().max().x = 1.0f - n->box().max().x;
                    }
                    if(vflip){
                        n->box().min().y = 1.0f - n->box().min().y;
                        n->box().max().y = 1.0f - n->box().max().y;
                    }
                    obj->mesh()->add(n);
                    if(fatal)
                        m_pPartitioner->register_object(n, FATAL);
                    else
                        m_pPartitioner->register_object(n, STATIC);
                    obj_cfg->set<string>("static", "");
                } else {
                    m_pPartitioner->register_object(obj->mesh(), GROUND);
                    obj->mesh()->config()->set<string>("static", "");
                }
            }
        }
    }
    
    setup_player(m_pChar);

    m_pPartitioner->on_collision(
        CHARACTER, STATIC,
        std::bind(&Game::cb_to_tile, this, _::_1, _::_2)
    );
    //m_pPartitioner->on_collision(
    //    THING, STATIC,
    //    std::bind(&Thing::cb_to_static, _::_1, _::_2)
    //);
    m_pPartitioner->on_collision(
        BULLET, STATIC,
        std::bind(&Game::cb_bullet_to_static, this, _::_1, _::_2)
    );
    //m_pPartitioner->on_collision(
    //    THING, BULLET,
    //    std::bind(&Thing::cb_to_bullet, _::_1, _::_2)
    //);
    //m_pPartitioner->on_collision(
    //    CHARACTER, THING,
    //    std::bind(&Thing::cb_to_player, _::_1, _::_2)
    //);

    m_JumpTimer.set(Freq::Time::ms(0));
}

void Game :: reset()
{
    m_pChar->position(glm::vec3(0.0f, 0.0f, 0.0f));
}

Game :: ~Game()
{
    m_pPipeline->partitioner()->clear();
}

void Game :: setup_player(std::shared_ptr<Sprite> player)
{
    // create masks
    auto n = make_shared<Node>();
    n->name("mask");
    n->box() = Box(
        vec3(-4.0f, -14.0f, -5.0f),
        vec3(4.0f,2.0f, 5.0f)
    );
    player->add(n);
    m_pPartitioner->register_object(n, CHARACTER);

    // create masks
    n = make_shared<Node>();
    n->name("feetmask");
    n->box() = Box(
        vec3(-4.0f, 0.0f, -5.0f),
        vec3(4.0f,4.0f, 5.0f)
    );
    player->add(n);
    m_pPartitioner->register_object(n, CHARACTER_FEET);

    n = make_shared<Node>();
    n->name("sidemask");
    n->box() = Box(
        vec3(-10.0f, -10.0f, -5.0f),
        vec3(10.0f,-2.0f, 5.0f)
    );
    player->add(n);
    m_pPartitioner->register_object(n, CHARACTER_SIDES);

    //setup_player_to_map(player);
    //for(auto&& thing: m_Things)
    //    setup_player_to_thing(player,thing);
}

void Game :: cb_to_static(Node* a, Node* b, Node* m)
{
    if(not m) m = a;
    
    auto p = m->position(Space::PARENT);
    auto v = m->velocity();
    auto col = [this, a, b]() -> bool {
        return (not m_pPartitioner->get_collisions_for(a, STATIC).empty()) ||
            a->world_box().collision(b->world_box());
    };

    //Box overlap = a->world_box().intersect(b->world_box());
    auto old_pos = Matrix::translation(kit::safe_ptr(m->snapshot(0))->world_transform);
    
    //vec3 overlap_sz = overlap.size() + vec3(1.0f);

    //if(not floatcmp(m->velocity().y, 0.0f))
    //{
        auto np = vec3(p.x, old_pos.y, p.z);
        m->position(np);
        if(not col()){
            m->velocity(glm::vec3(v.x,0.0f,v.z));
            return;
        }
    //}
        
    //if(not floatcmp(m->velocity().x, 0.0f))
    //{
        np = vec3(old_pos.x, p.y, p.z);
        m->position(np);
        if(not col())
            return;
    //}
    
    m->position(vec3(old_pos.x, old_pos.y, p.z));
    m->velocity(glm::vec3(0.0f));
}


void Game :: cb_to_tile(Node* a, Node* b)
{
    cb_to_static(a, b, a->parent());
}

void Game :: cb_bullet_to_static(Node* a, Node* b)
{
    Node* bullet = a->parent();
    //sound(bullet, "hit.wav");
    bullet->on_tick.connect([bullet](Freq::Time){
        bullet->detach();
    });
}

void Game :: enter()
{
    m_pMusic->play();
    
    m_Shader = m_pPipeline->load_shaders({"lit"});
    m_pPipeline->override_shader(PassType::NORMAL, m_Shader);
    for(int i=0; i<2; ++i){
        m_pQor->pipeline()->shader(i)->use();
        int u = m_pQor->pipeline()->shader(i)->uniform("Ambient");
        if(u >= 0)
            m_pQor->pipeline()->shader(i)->uniform(u,0.1f);
    }
    m_pPipeline->override_shader(PassType::NORMAL, (unsigned)PassType::NONE);
    
    m_pCamera->ortho();
    m_pPipeline->blend(false);
    m_pPipeline->winding(true);
    m_pPipeline->bg_color(Color::black());
    m_pInput->relative_mouse(false);
    m_pChar->acceleration(glm::vec3(0.0f, 500.0f, 0.0f));
}

void Game :: logic(Freq::Time t)
{
    Actuation::logic(t);
    
    if(m_pInput->key(SDLK_ESCAPE))
        m_pQor->quit();

    auto feet_colliders = m_pPartitioner->get_collisions_for(
        m_pChar->hook("feetmask").at(0), STATIC
    );
    auto wall_colliders = m_pPartitioner->get_collisions_for(
        m_pChar->hook("sidemask").at(0), STATIC
    );
    if(not feet_colliders.empty() && wall_colliders.empty()){
        auto v = m_pChar->velocity();
        m_pChar->velocity(0.0f, v.y, v.z);
    }
    
    auto vel = m_pChar->velocity();
    bool in_air = feet_colliders.empty();
    bool walljump = feet_colliders.empty() && not wall_colliders.empty();
    if(walljump)
        m_pChar->set_state("walljump");
    else if(in_air)
        m_pChar->set_state("jump");
    
    glm::vec3 move(0.0f);
    if(m_pChar->velocity().x > -K_EPSILON && m_pChar->velocity().x < K_EPSILON){
        if(m_pController->button("left")){
            m_pCamera->track(m_pCharFocusLeft);
            move += glm::vec3(-1.0f, 0.0f, 0.0f);
        }
        if(m_pController->button("right")){
            m_pCamera->track(m_pCharFocusRight);
            move += glm::vec3(1.0f, 0.0f, 0.0f);
        }
    }

    if(m_pController->button("shoot").pressed_now()){
        Sound::play(m_pCamera.get(), "shoot.wav", m_pResources);
    }
        
    bool block_jump = false;
    if(m_pController->button("up") || m_pController->button("jump")){
        
        if(walljump || not in_air || not m_JumpTimer.elapsed())
        {
            float x = 0.0f;
            if(walljump){
                auto last_dir = m_LastWallJumpDir;
                auto tile_loc = wall_colliders.at(0)->world_box().center().x;
                if(tile_loc < m_pChar->position(Space::WORLD).x + 4.0f){
                    m_LastWallJumpDir = -1;
                //    LOG("left");
                //    m_pChar->set_state("left");
                //    x += (move.x < -K_EPSILON) ? 100.0f : 0.0f;
                //}
                }else{
                    m_LastWallJumpDir = 1;
                //    LOG("right");
                //    m_pChar->set_state("right");
                //    x += (move.x > K_EPSILON) ? -100.0f : 0.0f;
                }

                // prevent "climbing" the wall by checking to make sure last wall jump was a diff direction (or floor)
                // 0 means floors, -1 and 1 are directions
                if(m_LastWallJumpDir != 0 && last_dir != 0 && m_LastWallJumpDir == last_dir)
                    block_jump = true;
                //move = vec3(0.0f);
            }
        
            // jumping from floor or from a different wall
            if(not block_jump){
                if(not in_air || walljump || not m_JumpTimer.elapsed()){
                    m_pChar->velocity(glm::vec3(x, -125.0f, 0.0f));
                    if(not in_air || walljump){
                        auto sounds = m_pCamera->hook_type<Sound>();
                        if(sounds.empty())
                            Sound::play(m_pCamera.get(), "jump.wav", m_pResources);
                        m_JumpTimer.set(Freq::Time::ms(200));
                    }
                }
            }else{
                // jumping on same wall? only allow this if jump timer is still running
                if(not m_JumpTimer.elapsed())
                    m_pChar->velocity(glm::vec3(x, -125.0f, 0.0f));
            }
        }
    }else{
        m_JumpTimer.set(Freq::Time::ms(0));
    }

    if(not in_air && m_WasInAir)
        Sound::play(m_pCamera.get(), "touch.wav", m_pResources);
    m_WasInAir = in_air;
    //if(m_pController->button("down"))
    //    move += glm::vec3(0.0f, 1.0f, 0.0f);
    
    if(not in_air)
        m_LastWallJumpDir = 0;
    if(glm::length(move) > K_EPSILON)
    {
        if(not in_air)
            m_pChar->set_state("walk");
        move = glm::normalize(move);
        if(move.x < -K_EPSILON)
            m_pChar->set_state("left");
        else if(move.x > K_EPSILON)
            m_pChar->set_state("right");
        move *= 100.0f * t.s();
        m_pChar->clear_snapshots();
        m_pChar->snapshot();
        m_pChar->move(move);
    }
    else
    {
        if(not in_air)
            m_pChar->set_state("stand");
        m_pChar->clear_snapshots();
        m_pChar->snapshot();
        //m_pChar->move(glm::vec3(0.0f));
    }
    m_pRoot->logic(t);
}

void Game :: render() const
{
    m_pPipeline->override_shader(PassType::NORMAL, m_Shader);
    m_pPipeline->render(m_pRoot.get(), m_pCamera.get(), nullptr, Pipeline::LIGHTS);
    m_pPipeline->override_shader(PassType::NORMAL, (unsigned)PassType::NONE);
}

