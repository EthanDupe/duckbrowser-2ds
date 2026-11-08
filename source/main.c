/*
 * EXO - Nintendo 2DS XL / 3DS Port (citro2d + libctru + NDSP)
 * Original HTML5 js13k game by Jack Oatley & Anton K. (ai Doge) | Music by Francis Fonye
 * Source visual fidelity matched to https://github.com/scorp200/td13k
 *
 * Authentic Original Exo Visuals & Dual-Screen Architecture:
 *  - Top Screen (400x240):
 *      * Radial space glow (#234 -> #123 -> #000) & dual parallax starscape (starscape.js)
 *      * Original "e X o" glowing title emblem (loadingscreen.js)
 *      * Tilted dashed orbits colored per celestial body (graphics.js)
 *      * Directional crescent shadows cast away from the Sun & unit health bars
 *      * Authentic vector sprites from src/data/spr*.js:
 *          - sprSatellite (blue twin solar panels + truss + hub)
 *          - sprMiningStation (red twin collector panels + truss + mining beam)
 *          - sprDefensePlatform & sprDefensePlatformLaser (spire + crimson saucer + twin-barrel turret)
 *          - sprOnlineIndicator (translucent #0F0 / #F00 status ring above orbitals)
 *          - sprEnemyShip (red dart fighters with 3D z-tilt elevation) & sprRocket
 *  - Bottom Touch Screen (320x240):
 *      * High-contrast black & white-bordered Exo GUI (buttons.js / gui.js)
 *      * Interactive Radar Minimap, Unit Inspector & Upgrade Panel, and 7-Button
 *        Construction Dock featuring the original white vector icons (spr*Icon.js).
 */

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TOP_W 400.0f
#define TOP_H 240.0f
#define BOT_W 320.0f
#define BOT_H 240.0f

#define PI_F 3.14159265f
#define TWO_PI_F 6.28318530f

#define MAKE_COLOR(r, g, b, a) \
    ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))

#define MAX_BODIES      256
#define MAX_ENEMIES     512
#define MAX_PARTICLES   1024
#define MAX_ROCKETS     128
#define MAX_LINKS       512

typedef enum {
    BODY_SUN      = 0,
    BODY_PLANET   = 1,
    BODY_SAT      = 2,
    BODY_MINER    = 3,
    BODY_PLATFORM = 4,
    BODY_MOON     = 5
} BodyType;

typedef enum {
    WPN_LASER     = 0,
    WPN_BEAM      = 1,
    WPN_ROCKET    = 2,
    WPN_EMP       = 3,
    WPN_LIGHTNING = 4
} WeaponType;

typedef struct {
    int active;
    int id;
    BodyType type;
    char name[32];
    float x, y;
    float size;
    u32 color;
    float hp;
    int online;
    float turret_angle;

    /* Orbit info */
    int has_orbit;
    int parent_idx;
    float orbit_radius;
    float orbit_speed;
    float orbit_angle;

    /* Weapon info (for BODY_PLATFORM) */
    int has_weapon;
    WeaponType wpn_type;
    int wpn_level;
    int cooldown;
    float emp_radius;
    float beam_dir;
    int beam_active;
    int lightning_pts[12];
    int lightning_count;
} Body;

typedef struct {
    int active;
    float x, y, z;
    float angle;
    float hp;
    int target_idx;
    int fire_cd;
    float slow_factor;
    int slow_timer;
} Enemy;

typedef struct {
    int active;
    float x, y;
    float angle;
    float dist_left;
    float speed;
    u32 color;
} Particle;

typedef struct {
    int active;
    float x, y;
    float angle;
    int life;
    u32 color;
} Rocket;

typedef struct {
    u8 *data;
    u32 size;
    u32 sample_rate;
    u16 channels;
    u16 bits_per_sample;
    ndspWaveBuf wave_buf;
    int loaded;
} WavSound;

/* Global Game State */
static Body g_bodies[MAX_BODIES];
static int g_body_count = 0;
static int g_base_idx = 0;
static float g_system_size = 2500.0f;

static Enemy g_enemies[MAX_ENEMIES];
static Particle g_particles[MAX_PARTICLES];
static Rocket g_rockets[MAX_ROCKETS];
static int g_links[MAX_LINKS][2];
static int g_link_count = 0;

/* Player Resources (mirrors `Base` in src/base.js) */
static float g_minerals = 50.0f;
static float g_energy = 10.0f;
static const float g_mineral_rate = 0.025f;
static const float g_energy_rate = 0.05f;
static const float g_relay_range = 501.0f;

/* Wave Spawner (`WaveManager.js`) */
static int g_wave_num = 0;
static int g_wave_spawn_count = 30;
static int g_wave_interval_sec = 60;
static int g_wave_timer_frames = 3600;

/* Camera & Cursor (`View.js` and `Mouse.js`) */
static float g_cam_x = 0.0f, g_cam_y = 0.0f;
static float g_cam_tx = 0.0f, g_cam_ty = 0.0f;
static float g_zoom = 0.35f, g_target_zoom = 0.35f;
static float g_tilt = 1.25f;
static float g_cursor_wx = 0.0f, g_cursor_wy = 0.0f;

/* UI & Construction State (`Build.js` and `gui.js`) */
static int g_state = 0; /* 0 = Title, 1 = Playing, 2 = Paused */
static int g_selected_idx = -1;
static int g_build_active = 0;
static BodyType g_build_type = BODY_SAT;
static WeaponType g_build_wpn = WPN_LASER;
static int g_build_cost = 0;
static int g_build_parent = -1;
static float g_build_radius = 100.0f;
static int g_build_valid = 0;
static u32 g_frame_tick = 0;

static char g_status_msg[64] = "Welcome to Exo";
static int g_status_timer = 240;

/* Starfield vertices (`starscape.js` - 240 stars across two parallax layers) */
typedef struct { float x, y; u8 layer; u32 color; } Star;
static Star g_stars[240];

/* Audio */
static int g_ndsp_ok = 0;
static int g_music_muted = 0;
static int g_sfx_muted = 0;
static WavSound g_snd_music;
static WavSound g_snd_select;
static WavSound g_snd_laser[3];

/* Text rendering */
static C2D_TextBuf g_text_buf;

/* Helper Math */
static float randf(void) {
    return (float)rand() / (float)RAND_MAX;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float dist_sq(float x1, float y1, float x2, float y2) {
    float dx = x1 - x2, dy = y1 - y2;
    return dx * dx + dy * dy;
}

static float dist_f(float x1, float y1, float x2, float y2) {
    return sqrtf(dist_sq(x1, y1, x2, y2));
}

static float angle_between(float x1, float y1, float x2, float y2) {
    return atan2f(y1 - y2, x1 - x2);
}

static float wrap_two_pi(float a) {
    return a - floorf(a / TWO_PI_F) * TWO_PI_F;
}

static u32 hsl_to_color(float h_deg, float s_pct, float l_pct) {
    float h = fmodf(h_deg, 360.0f) / 360.0f;
    if (h < 0) h += 1.0f;
    float s = clampf(s_pct / 100.0f, 0.0f, 1.0f);
    float l = clampf(l_pct / 100.0f, 0.0f, 1.0f);
    float r, g, b;
    if (s == 0.0f) {
        r = g = b = l;
    } else {
        float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        float t[3] = { h + 1.0f / 3.0f, h, h - 1.0f / 3.0f };
        float c[3];
        for (int i = 0; i < 3; i++) {
            if (t[i] < 0.0f) t[i] += 1.0f;
            if (t[i] > 1.0f) t[i] -= 1.0f;
            if (t[i] < 1.0f / 6.0f) c[i] = p + (q - p) * 6.0f * t[i];
            else if (t[i] < 0.5f)   c[i] = q;
            else if (t[i] < 2.0f / 3.0f) c[i] = p + (q - p) * (2.0f / 3.0f - t[i]) * 6.0f;
            else c[i] = p;
        }
        r = c[0]; g = c[1]; b = c[2];
    }
    return MAKE_COLOR((u8)(r * 255), (u8)(g * 255), (u8)(b * 255), 255);
}

static u32 color_with_alpha(u32 col, u8 alpha) {
    return (col & 0x00FFFFFFu) | ((u32)alpha << 24);
}

static void set_status(const char *msg) {
    snprintf(g_status_msg, sizeof(g_status_msg), "%s", msg);
    g_status_timer = 210;
}

/* Audio Loader & Player */
static int wav_load(WavSound *snd, const char *path, bool loop) {
    memset(snd, 0, sizeof(*snd));
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    char riff[4], wave[4];
    u32 file_size = 0;
    if (fread(riff, 1, 4, f) != 4 ||
        fread(&file_size, 4, 1, f) != 1 ||
        fread(wave, 1, 4, f) != 4) {
        fclose(f);
        return -1;
    }
    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(wave, "WAVE", 4) != 0) {
        fclose(f);
        return -1;
    }

    int got_data = 0;
    while (!got_data && !feof(f)) {
        char chunk_id[4];
        u32 chunk_size = 0;
        if (fread(chunk_id, 1, 4, f) != 4) break;
        if (fread(&chunk_size, 4, 1, f) != 1) break;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            u16 audio_fmt = 0;
            fread(&audio_fmt, 2, 1, f);
            fread(&snd->channels, 2, 1, f);
            fread(&snd->sample_rate, 4, 1, f);
            u32 byte_rate = 0;
            u16 block_align = 0;
            fread(&byte_rate, 4, 1, f);
            fread(&block_align, 2, 1, f);
            fread(&snd->bits_per_sample, 2, 1, f);
            if (chunk_size > 16) fseek(f, chunk_size - 16, SEEK_CUR);
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            snd->data = (u8 *)linearAlloc(chunk_size);
            if (!snd->data) {
                fclose(f);
                return -1;
            }
            snd->size = chunk_size;
            fread(snd->data, 1, chunk_size, f);
            got_data = 1;
        } else {
            fseek(f, chunk_size, SEEK_CUR);
        }
    }
    fclose(f);
    if (!got_data || !snd->data) return -1;

    snd->wave_buf.data_vaddr = snd->data;
    snd->wave_buf.nsamples = snd->size / (snd->channels * (snd->bits_per_sample / 8));
    snd->wave_buf.looping = loop;
    snd->wave_buf.status = NDSP_WBUF_FREE;
    DSP_FlushDataCache(snd->data, snd->size);
    snd->loaded = 1;
    return 0;
}

static void audio_init(void) {
    if (ndspInit() != 0) {
        g_ndsp_ok = 0;
        return;
    }
    g_ndsp_ok = 1;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);

    wav_load(&g_snd_music, "romfs:/audio/music.wav", true);
    wav_load(&g_snd_select, "romfs:/audio/select.wav", false);
    wav_load(&g_snd_laser[0], "romfs:/audio/laser0.wav", false);
    wav_load(&g_snd_laser[1], "romfs:/audio/laser1.wav", false);
    wav_load(&g_snd_laser[2], "romfs:/audio/laser2.wav", false);

    if (g_snd_music.loaded) {
        ndspChnReset(0);
        ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
        ndspChnSetRate(0, (float)g_snd_music.sample_rate);
        ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
        float mix[12] = {0};
        mix[0] = 0.55f; mix[1] = 0.55f;
        ndspChnSetMix(0, mix);
        ndspChnWaveBufAdd(0, &g_snd_music.wave_buf);
    }

    for (int ch = 1; ch <= 4; ch++) {
        ndspChnReset(ch);
        ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
        ndspChnSetRate(ch, 44100.0f);
        ndspChnSetFormat(ch, NDSP_FORMAT_STEREO_PCM16);
        float mix[12] = {0};
        mix[0] = 0.45f; mix[1] = 0.45f;
        ndspChnSetMix(ch, mix);
    }
}

static void audio_play_sfx(WavSound *snd, int ch) {
    if (!g_ndsp_ok || g_sfx_muted || !snd || !snd->loaded) return;
    ndspChnWaveBufClear(ch);
    snd->wave_buf.status = NDSP_WBUF_FREE;
    DSP_FlushDataCache(snd->data, snd->size);
    ndspChnSetRate(ch, (float)snd->sample_rate);
    ndspChnWaveBufAdd(ch, &snd->wave_buf);
}

static void audio_cleanup(void) {
    if (g_ndsp_ok) {
        for (int ch = 0; ch <= 4; ch++) ndspChnReset(ch);
        ndspExit();
    }
    if (g_snd_music.data) linearFree(g_snd_music.data);
    if (g_snd_select.data) linearFree(g_snd_select.data);
    for (int i = 0; i < 3; i++) {
        if (g_snd_laser[i].data) linearFree(g_snd_laser[i].data);
    }
}

/* Particle & Explosion System (`Laser.js` and `Explosion.js`) */
static void spawn_particle(float x, float y, float angle, float dist, u32 color, float speed) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!g_particles[i].active) {
            g_particles[i].active = 1;
            g_particles[i].x = x;
            g_particles[i].y = y;
            g_particles[i].angle = angle;
            g_particles[i].dist_left = dist - 50.0f;
            g_particles[i].speed = speed > 0.0f ? speed : 50.0f;
            g_particles[i].color = color;
            return;
        }
    }
}

static void spawn_explosion(float x, float y) {
    u32 col = MAKE_COLOR(255, 255, 0, 255);
    for (int i = 0; i < 36; i++) {
        spawn_particle(x, y, randf() * TWO_PI_F, 200.0f, col, 5.0f + 5.0f * randf());
    }
}

static void spawn_rocket(float x, float y, float angle, float dist, u32 color) {
    for (int i = 0; i < MAX_ROCKETS; i++) {
        if (!g_rockets[i].active) {
            g_rockets[i].active = 1;
            g_rockets[i].x = x;
            g_rockets[i].y = y;
            g_rockets[i].angle = angle;
            g_rockets[i].life = (int)(dist - 20.0f);
            g_rockets[i].color = color;
            return;
        }
    }
}

/* Enemy System (`Enemy.js`) */
static void spawn_enemy(float x, float y, float angle) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!g_enemies[i].active) {
            g_enemies[i].active = 1;
            g_enemies[i].x = x;
            g_enemies[i].y = y;
            g_enemies[i].z = 20.0f * randf();
            g_enemies[i].angle = angle;
            g_enemies[i].hp = 10.0f;
            g_enemies[i].target_idx = -1;
            g_enemies[i].fire_cd = (int)(10.0f * randf());
            g_enemies[i].slow_factor = 1.0f;
            g_enemies[i].slow_timer = 0;
            return;
        }
    }
}

static int find_nearest_enemy(float x, float y, float max_dist) {
    int best = -1;
    float best_d = max_dist;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!g_enemies[i].active) continue;
        float d = dist_f(x, y, g_enemies[i].x, g_enemies[i].y);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* Body & Building System (`orbital.js` & `Build.js`) */
static int get_build_cost(BodyType type, WeaponType wpn) {
    if (type == BODY_SAT) return 30;
    if (type == BODY_MINER) return 50;
    if (type == BODY_PLATFORM) {
        switch (wpn) {
            case WPN_LASER:     return 100;
            case WPN_BEAM:      return 150;
            case WPN_ROCKET:    return 200;
            case WPN_EMP:       return 250;
            case WPN_LIGHTNING: return 350; /* Matches zapModule.js cost = 350 */
        }
    }
    return 0;
}

static const char *get_build_name(BodyType type, WeaponType wpn) {
    if (type == BODY_SAT) return "Satellite";
    if (type == BODY_MINER) return "Mining Station";
    if (type == BODY_PLATFORM) {
        switch (wpn) {
            case WPN_LASER:     return "Laser Platform";
            case WPN_BEAM:      return "Beam Platform";
            case WPN_ROCKET:    return "Rocket Platform";
            case WPN_EMP:       return "EMP Platform";
            case WPN_LIGHTNING: return "Lightning Platform";
        }
    }
    return "Station";
}

static int create_body(BodyType type, const char *name, u32 color, float size,
                       float x, float y, int parent_idx, float orbit_r,
                       float orbit_spd, float orbit_ang) {
    if (g_body_count >= MAX_BODIES) return -1;
    int idx = g_body_count++;
    Body *b = &g_bodies[idx];
    memset(b, 0, sizeof(*b));
    b->active = 1;
    b->id = idx;
    b->type = type;
    snprintf(b->name, sizeof(b->name), "%s", name);
    b->x = x;
    b->y = y;
    b->size = size;
    b->color = color;
    b->hp = 100.0f;
    b->online = 0;
    b->emp_radius = 400.0f;

    if (parent_idx >= 0) {
        b->has_orbit = 1;
        b->parent_idx = parent_idx;
        b->orbit_radius = orbit_r;
        b->orbit_speed = (orbit_spd / 2.0f) / (orbit_r / 100.0f);
        if (type == BODY_PLANET) {
            b->orbit_speed /= (orbit_r / 10000.0f);
        }
        b->orbit_angle = orbit_ang;
        b->x = g_bodies[parent_idx].x + cosf(orbit_ang) * orbit_r;
        b->y = g_bodies[parent_idx].y + sinf(orbit_ang) * orbit_r;
    }
    return idx;
}

static void set_platform_weapon(int idx, WeaponType wpn, int level) {
    if (idx < 0 || idx >= g_body_count) return;
    Body *b = &g_bodies[idx];
    b->has_weapon = 1;
    b->wpn_type = wpn;
    b->wpn_level = level;
    b->cooldown = 0;
    b->emp_radius = 400.0f;
    b->beam_active = 0;
    b->lightning_count = 0;
}

static int find_nearest_body(float wx, float wy, int only_planet_or_sun) {
    int best = -1;
    float best_d = 999999.0f;
    for (int i = 0; i < g_body_count; i++) {
        if (!g_bodies[i].active) continue;
        if (only_planet_or_sun && g_bodies[i].type != BODY_SUN && g_bodies[i].type != BODY_PLANET)
            continue;
        float d = dist_f(wx, wy, g_bodies[i].x, g_bodies[i].y);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* Power Relay Graph (`orbital.js`) */
static void update_power_network(void) {
    for (int i = 0; i < g_body_count; i++) {
        g_bodies[i].online = 0;
    }
    g_link_count = 0;
    if (g_base_idx < 0 || !g_bodies[g_base_idx].active) return;

    int queue[MAX_BODIES];
    int visited[MAX_BODIES] = {0};
    int q_head = 0, q_tail = 0;

    g_bodies[g_base_idx].online = 1;
    visited[g_base_idx] = 1;
    queue[q_tail++] = g_base_idx;

    while (q_head < q_tail) {
        int cur = queue[q_head++];
        Body *a = &g_bodies[cur];
        if (cur != g_base_idx && a->type != BODY_SAT) continue;

        for (int i = 0; i < g_body_count; i++) {
            if (!g_bodies[i].active || i == cur || visited[i]) continue;
            Body *d = &g_bodies[i];
            if (d->type != BODY_SAT && d->type != BODY_MINER && d->type != BODY_PLATFORM)
                continue;
            if (dist_f(a->x, a->y, d->x, d->y) <= g_relay_range) {
                d->online = 1;
                visited[i] = 1;
                if (g_link_count < MAX_LINKS) {
                    g_links[g_link_count][0] = cur;
                    g_links[g_link_count][1] = i;
                    g_link_count++;
                }
                queue[q_tail++] = i;
            }
        }
    }
}

/* Generate Solar System (`SystemGenerator.js`) */
static void init_solar_system(void) {
    g_body_count = 0;
    memset(g_bodies, 0, sizeof(g_bodies));
    memset(g_enemies, 0, sizeof(g_enemies));
    memset(g_particles, 0, sizeof(g_particles));
    memset(g_rockets, 0, sizeof(g_rockets));

    int sun = create_body(BODY_SUN, "The Sun", hsl_to_color(60, 100, 50), 100.0f, 0.0f, 0.0f, -1, 0, 0, 0);
    int num_planets = 2 + (rand() % 3);
    float d = 1000.0f;
    int first_planet = -1;

    for (int f = 0; f < num_planets; f++) {
        float p_size = 20.0f + (float)(rand() % 30);
        u32 p_col = hsl_to_color(randf() * 360.0f, 39.0f, 62.0f);
        int p_idx = create_body(BODY_PLANET, "Planet", p_col, p_size, 0, 0, sun, d, 0.003f, randf() * TWO_PI_F);
        if (first_planet < 0) first_planet = p_idx;

        d += 100.0f * (float)((500 + (rand() % 500)) / 100);
        int num_moons = rand() % 3;
        float m_dist = 200.0f;
        for (int l = 0; l < num_moons; l++) {
            float m_size = floorf(0.1f * p_size + randf() * p_size * 0.3f);
            if (m_size < 4.0f) m_size = 4.0f;
            u32 m_col = hsl_to_color(randf() * 360.0f, 39.0f, 62.0f);
            create_body(BODY_MOON, "Moon", m_col, m_size, 0, 0, p_idx, m_dist, 0.01f, randf() * TWO_PI_F);
            m_dist += 100.0f;
        }
    }

    g_system_size = d;
    g_base_idx = first_planet;
    snprintf(g_bodies[g_base_idx].name, sizeof(g_bodies[g_base_idx].name), "Base Planet");
    g_bodies[g_base_idx].hp = 100.0f;

    /* Spawn 3 initial Mining Stations around Base Planet */
    create_body(BODY_MINER, "Mining Station", hsl_to_color(212, 100, 97), 5.0f, 0, 0, g_base_idx, 100.0f, 0.01f, 0.33f * TWO_PI_F);
    create_body(BODY_MINER, "Mining Station", hsl_to_color(212, 100, 97), 5.0f, 0, 0, g_base_idx, 100.0f, 0.01f, 0.66f * TWO_PI_F);
    create_body(BODY_MINER, "Mining Station", hsl_to_color(212, 100, 97), 5.0f, 0, 0, g_base_idx, 100.0f, 0.01f, 0.99f * TWO_PI_F);

    g_minerals = 50.0f;
    g_energy = 10.0f;
    g_wave_num = 0;
    g_wave_spawn_count = 30;
    g_wave_interval_sec = 60;
    g_wave_timer_frames = 60 * g_wave_interval_sec;

    g_cam_x = g_cam_tx = g_bodies[g_base_idx].x;
    g_cam_y = g_cam_ty = g_bodies[g_base_idx].y;
    g_zoom = g_target_zoom = 0.35f;
    g_cursor_wx = g_cam_x;
    g_cursor_wy = g_cam_y;
    g_selected_idx = g_base_idx;
    g_build_active = 0;

    update_power_network();
}

/* Weapon Update Logic (`src/objects/orbital/modules/`) */
static void update_platform_weapon(Body *b) {
    if (!b->online || !b->has_weapon) return;
    float lvl_mult = 1.0f + 0.35f * (float)(b->wpn_level - 1);

    switch (b->wpn_type) {
        case WPN_LASER: {
            if (b->cooldown > 0) {
                b->cooldown--;
            } else {
                b->cooldown = 2;
                int e_idx = find_nearest_enemy(b->x, b->y, 1000.0f);
                if (e_idx >= 0 && g_energy >= 0.05f) {
                    g_energy -= 0.05f;
                    Enemy *e = &g_enemies[e_idx];
                    int miss = (randf() > 0.5f);
                    float d = miss ? 2000.0f : dist_f(b->x, b->y, e->x, e->y);
                    float ang = angle_between(b->x, b->y, e->x, e->y);
                    b->turret_angle = ang;
                    spawn_particle(b->x, b->y, ang, d, MAKE_COLOR(0, 255, 0, 255), 50.0f);
                    if (!miss) e->hp -= 2.0f * lvl_mult;
                    if (randf() > 0.7f) {
                        int sfx = rand() % 3;
                        audio_play_sfx(&g_snd_laser[sfx], 2 + sfx);
                    }
                }
            }
            break;
        }
        case WPN_BEAM: {
            b->beam_active = 0;
            if (g_energy >= 0.1f) {
                int e_idx = find_nearest_enemy(b->x, b->y, 1000.0f);
                if (e_idx >= 0) {
                    g_energy -= 0.1f;
                    b->beam_active = 1;
                    Enemy *target = &g_enemies[e_idx];
                    float f = angle_between(b->x, b->y, target->x, target->y);
                    float diff = wrap_two_pi(f - b->beam_dir + PI_F) - PI_F;
                    b->beam_dir += 0.02f * (diff >= 0.0f ? 1.0f : -1.0f);
                    b->turret_angle = b->beam_dir;
                    for (int i = 0; i < MAX_ENEMIES; i++) {
                        if (!g_enemies[i].active) continue;
                        float d = dist_f(b->x, b->y, g_enemies[i].x, g_enemies[i].y);
                        float k = wrap_two_pi(b->beam_dir - angle_between(b->x, b->y, g_enemies[i].x, g_enemies[i].y) + PI_F) - PI_F;
                        if (d < 1000.0f && fabsf(k) < 0.12f) {
                            g_enemies[i].hp -= 0.5f * lvl_mult;
                        }
                    }
                }
            }
            break;
        }
        case WPN_ROCKET: {
            if (b->cooldown > 0) {
                b->cooldown--;
            } else {
                b->cooldown = 20;
                int e_idx = find_nearest_enemy(b->x, b->y, 1500.0f);
                if (e_idx >= 0 && g_energy >= 0.1f) {
                    g_energy -= 0.1f;
                    Enemy *e = &g_enemies[e_idx];
                    float ang = angle_between(b->x, b->y, e->x, e->y);
                    b->turret_angle = ang;
                    spawn_rocket(b->x, b->y, ang, dist_f(b->x, b->y, e->x, e->y), MAKE_COLOR(0, 255, 0, 255));
                }
            }
            break;
        }
        case WPN_EMP: {
            if (b->cooldown > 0) b->cooldown--;
            if (b->cooldown <= 0 && g_energy >= 30.0f && find_nearest_enemy(b->x, b->y, 400.0f) >= 0) {
                b->emp_radius = 0.0f;
                b->cooldown = 120;
                g_energy -= 30.0f;
                for (int i = 0; i < MAX_ENEMIES; i++) {
                    if (!g_enemies[i].active) continue;
                    if (dist_f(b->x, b->y, g_enemies[i].x, g_enemies[i].y) < 400.0f) {
                        g_enemies[i].slow_factor = 0.05f;
                        g_enemies[i].slow_timer = 100 + 30 * (b->wpn_level - 1);
                    }
                }
            }
            if (b->emp_radius < 400.0f) b->emp_radius += 20.0f;
            break;
        }
        case WPN_LIGHTNING: {
            b->lightning_count = 0;
            int first = find_nearest_enemy(b->x, b->y, 300.0f);
            if (first >= 0) {
                int cur_e = first;
                for (int i = 0; i < MAX_ENEMIES && g_energy >= 0.01f && b->lightning_count < 10; i++) {
                    if (!g_enemies[i].active) continue;
                    if (dist_f(g_enemies[cur_e].x, g_enemies[cur_e].y, g_enemies[i].x, g_enemies[i].y) <= 300.0f) {
                        g_energy -= 0.01f;
                        cur_e = i;
                        b->lightning_pts[b->lightning_count++] = i;
                        g_enemies[i].hp -= 0.15f * lvl_mult;
                    }
                }
            }
            break;
        }
    }
}

/* Main Simulation Step (`game.js`) */
static void update_simulation(void) {
    g_frame_tick++;
    if (g_status_timer > 0) g_status_timer--;

    if (g_state != 1) return;

    if (g_base_idx < 0 || !g_bodies[g_base_idx].active || g_bodies[g_base_idx].hp <= 0.0f) {
        set_status("Base Lost! Rebooting expedition...");
        init_solar_system();
        return;
    }

    update_power_network();

    for (int i = 0; i < g_body_count; i++) {
        Body *b = &g_bodies[i];
        if (!b->active) continue;
        if (b->hp <= 0.0f && i != g_base_idx) {
            spawn_explosion(b->x, b->y);
            b->active = 0;
            if (g_selected_idx == i) g_selected_idx = -1;
            continue;
        }
        if (b->has_orbit && b->parent_idx >= 0 && g_bodies[b->parent_idx].active) {
            b->orbit_angle += b->orbit_speed;
            b->x = g_bodies[b->parent_idx].x + cosf(b->orbit_angle) * b->orbit_radius;
            b->y = g_bodies[b->parent_idx].y + sinf(b->orbit_angle) * b->orbit_radius;
        }
        if (b->online) {
            if (b->type == BODY_SAT) {
                float factor = clampf(1.0f - b->orbit_radius / 500.0f, 0.1f, 1.0f);
                g_energy += g_energy_rate * factor;
            } else if (b->type == BODY_MINER) {
                float factor = clampf(1.0f - b->orbit_radius / 500.0f, 0.1f, 1.0f);
                g_minerals += g_mineral_rate * factor;
            } else if (b->type == BODY_PLATFORM) {
                update_platform_weapon(b);
            }
        }
    }

    if (--g_wave_timer_frames <= 0) {
        for (int i = 0; i < g_wave_spawn_count; i++) {
            float k = randf() * TWO_PI_F;
            spawn_enemy(cosf(k) * g_system_size, sinf(k) * g_system_size, k);
        }
        g_wave_num++;
        g_wave_interval_sec = (g_wave_interval_sec - 2 > 2) ? (g_wave_interval_sec - 2) : 2;
        g_wave_spawn_count += 30;
        g_wave_timer_frames = 60 * g_wave_interval_sec;
        set_status("Alert: Hostile wave inbound!");
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        if (!e->active) continue;

        float speed = 5.0f;
        if (e->slow_timer > 0) {
            speed *= e->slow_factor;
            e->slow_timer--;
        }

        float best_d = 999999.0f;
        int best_t = g_base_idx;
        for (int j = 0; j < g_body_count; j++) {
            Body *b = &g_bodies[j];
            if (!b->active) continue;
            if ((b->type > BODY_PLANET || j == g_base_idx) && b->online) {
                float d = dist_f(e->x, e->y, b->x, b->y);
                if (d < best_d) {
                    best_d = d;
                    best_t = j;
                }
            }
        }
        e->target_idx = best_t;
        if (best_t >= 0 && g_bodies[best_t].active) {
            Body *t = &g_bodies[best_t];
            float target_ang = angle_between(e->x, e->y, t->x, t->y);
            e->angle += 0.02f * (wrap_two_pi(target_ang - e->angle + PI_F) - PI_F);
            e->x -= speed * cosf(e->angle);
            e->y -= speed * sinf(e->angle);

            if (--e->fire_cd <= 0) {
                e->fire_cd = 10;
                float d = dist_f(e->x, e->y, t->x, t->y);
                if (d < 500.0f) {
                    int miss = (randf() > 0.5f);
                    spawn_particle(e->x, e->y, target_ang, miss ? 2000.0f : d, MAKE_COLOR(255, 0, 0, 255), 50.0f);
                    if (!miss) t->hp -= 0.2f;
                }
            }
        }

        if (e->hp <= 0.0f) {
            spawn_explosion(e->x, e->y);
            e->active = 0;
            continue;
        }

        for (int j = i + 1; j < MAX_ENEMIES; j++) {
            if (!g_enemies[j].active) continue;
            if (dist_sq(e->x, e->y, g_enemies[j].x, g_enemies[j].y) < 200.0f) {
                float mx = (e->x - g_enemies[j].x) * 0.25f;
                float my = (e->y - g_enemies[j].y) * 0.25f;
                e->x += mx; e->y += my;
                g_enemies[j].x -= mx; g_enemies[j].y -= my;
                break;
            }
        }
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g_particles[i];
        if (!p->active) continue;
        p->dist_left -= p->speed;
        if (p->dist_left <= 0.0f) {
            p->active = 0;
        } else {
            p->x -= cosf(p->angle) * p->speed;
            p->y -= sinf(p->angle) * p->speed;
        }
    }

    for (int i = 0; i < MAX_ROCKETS; i++) {
        Rocket *r = &g_rockets[i];
        if (!r->active) continue;
        int e_idx = find_nearest_enemy(r->x, r->y, 500.0f);
        float d = (e_idx >= 0) ? dist_f(r->x, r->y, g_enemies[e_idx].x, g_enemies[e_idx].y) : 999.0f;
        if (--r->life <= 0 || d < 30.0f) {
            r->active = 0;
            spawn_explosion(r->x, r->y);
            if (e_idx >= 0 && d < 40.0f) {
                g_enemies[e_idx].hp -= 10.0f;
            }
        } else {
            if (e_idx >= 0) {
                float ang = angle_between(r->x, r->y, g_enemies[e_idx].x, g_enemies[e_idx].y);
                r->angle += 0.05f * (wrap_two_pi(ang - r->angle + PI_F) - PI_F);
            }
            r->x -= 20.0f * cosf(r->angle);
            r->y -= 20.0f * sinf(r->angle);
        }
    }
}

/* Drawing Primitives & Original Exo Visual Helpers */
static void draw_text(const char *str, float x, float y, float scale, u32 color, int align_center) {
    C2D_Text text;
    C2D_TextParse(&text, g_text_buf, str);
    C2D_TextOptimize(&text);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&text, scale, scale, &w, &h);
    float dx = align_center ? (x - w * 0.5f) : x;
    C2D_DrawText(&text, C2D_WithColor, dx, y, 0.0f, scale, scale, color);
}

static void world_to_top_screen(float wx, float wy, float *sx, float *sy) {
    *sx = TOP_W * 0.5f + (wx - g_cam_x) * g_zoom;
    *sy = TOP_H * 0.5f + ((wy - g_cam_y) / g_tilt) * g_zoom;
}

static void draw_circle_outline(float cx, float cy, float rx, float ry, int segs, u32 color, float thick) {
    float step = TWO_PI_F / (float)segs;
    float px = cx + rx;
    float py = cy;
    for (int i = 1; i <= segs; i++) {
        float a = i * step;
        float nx = cx + cosf(a) * rx;
        float ny = cy + sinf(a) * ry;
        C2D_DrawLine(px, py, color, nx, ny, color, thick, 0.0f);
        px = nx; py = ny;
    }
}

/* Dashed Tilted Orbit Ring (`ctxOrbit.setLineDash` in src/graphics.js) */
static void draw_dashed_orbit(float cx, float cy, float rx, float ry, int segs, u32 color, float thick) {
    float step = TWO_PI_F / (float)segs;
    for (int i = 0; i < segs; i += 2) {
        float a0 = (float)i * step;
        float a1 = (float)(i + 1) * step;
        C2D_DrawLine(cx + cosf(a0) * rx, cy + sinf(a0) * ry, color,
                     cx + cosf(a1) * rx, cy + sinf(a1) * ry, color, thick, 0.0f);
    }
}

/* Original Exo Box Button (`src/buttons.js`: rgba(0,0,0,0.5) fill + 2px #FFF stroke) */
static void draw_exo_box(float x, float y, float w, float h, u32 fill_col, u32 border_col, float border_w) {
    C2D_DrawRectSolid(x, y, 0.0f, w, h, fill_col);
    C2D_DrawRectSolid(x, y, 0.0f, w, border_w, border_col);
    C2D_DrawRectSolid(x, y + h - border_w, 0.0f, w, border_w, border_col);
    C2D_DrawRectSolid(x, y, 0.0f, border_w, h, border_col);
    C2D_DrawRectSolid(x + w - border_w, y, 0.0f, border_w, h, border_col);
}

/* Online / Offline Ring (`src/data/sprOnlineIndicator.js`) */
static void draw_online_indicator(float sx, float sy, int online) {
    u32 fill = online ? MAKE_COLOR(0, 255, 0, 52)  : MAKE_COLOR(255, 0, 0, 52);
    u32 rim  = online ? MAKE_COLOR(0, 255, 0, 235) : MAKE_COLOR(255, 0, 0, 235);
    C2D_DrawCircleSolid(sx, sy, 0.0f, 3.2f, fill);
    draw_circle_outline(sx, sy, 3.2f, 3.2f, 10, rim, 1.0f);
}

/* Health Bar above damaged units (`src/graphics.js`) */
static void draw_health_bar(float sx, float sy, float hp, float width) {
    if (hp >= 99.9f) return;
    float pct = clampf(hp / 100.0f, 0.0f, 1.0f);
    float x = sx - width * 0.5f;
    C2D_DrawRectSolid(x - 1.0f, sy - 1.0f, 0.0f, width + 2.0f, 4.5f, MAKE_COLOR(0, 0, 0, 200));
    C2D_DrawRectSolid(x, sy, 0.0f, width, 2.5f, MAKE_COLOR(180, 30, 30, 220));
    C2D_DrawRectSolid(x, sy, 0.0f, width * pct, 2.5f, MAKE_COLOR(0, 255, 0, 240));
}

/* Authentic World Sprites from `src/data/spr*.js` */
static void draw_spr_satellite(float sx, float sy, float scale, int online) {
    float s = clampf(scale, 0.55f, 1.8f);
    /* Central truss (#dddddd) */
    C2D_DrawRectSolid(sx - 1.0f * s, sy - 5.5f * s, 0.0f, 2.0f * s, 11.0f * s, MAKE_COLOR(221, 221, 221, 255));
    /* Upper & Lower Solar Panels (#639bff) */
    C2D_DrawRectSolid(sx - 2.6f * s, sy - 11.0f * s, 0.0f, 5.2f * s, 6.2f * s, MAKE_COLOR(99, 155, 255, 255));
    C2D_DrawRectSolid(sx - 2.6f * s, sy + 4.8f * s,  0.0f, 5.2f * s, 6.2f * s, MAKE_COLOR(99, 155, 255, 255));
    /* Metallic Core Hub (#9badb7) */
    C2D_DrawRectSolid(sx - 3.2f * s, sy - 1.4f * s, 0.0f, 6.4f * s, 2.8f * s, MAKE_COLOR(155, 173, 183, 255));
    draw_online_indicator(sx, sy - 13.5f * s, online);
}

static void draw_spr_mining_station(float sx, float sy, float scale, int online) {
    float s = clampf(scale, 0.55f, 1.8f);
    /* Central truss (#dddddd) */
    C2D_DrawRectSolid(sx - 1.0f * s, sy - 5.5f * s, 0.0f, 2.0f * s, 11.0f * s, MAKE_COLOR(221, 221, 221, 255));
    /* Upper & Lower Crimson Collector Panels (#bb3333) */
    C2D_DrawRectSolid(sx - 2.8f * s, sy - 8.2f * s, 0.0f, 5.6f * s, 4.2f * s, MAKE_COLOR(187, 51, 51, 255));
    C2D_DrawRectSolid(sx - 2.8f * s, sy + 4.0f * s, 0.0f, 5.6f * s, 4.2f * s, MAKE_COLOR(187, 51, 51, 255));
    /* Metallic Core Hub (#9badb7) */
    C2D_DrawRectSolid(sx - 3.4f * s, sy - 1.5f * s, 0.0f, 6.8f * s, 3.0f * s, MAKE_COLOR(155, 173, 183, 255));
    draw_online_indicator(sx, sy - 11.5f * s, online);
}

static void draw_spr_defense_platform(float sx, float sy, float scale, float turret_ang, int online) {
    float s = clampf(scale, 0.65f, 2.0f);
    /* Lower Dark Spire (#444444: (-20,0)->(0,50)->(20,0)) */
    C2D_DrawLine(sx - 3.2f * s, sy, MAKE_COLOR(68, 68, 68, 255),
                 sx, sy + 7.5f * s, MAKE_COLOR(68, 68, 68, 255), 3.2f * s, 0.0f);
    C2D_DrawLine(sx + 3.2f * s, sy, MAKE_COLOR(68, 68, 68, 255),
                 sx, sy + 7.5f * s, MAKE_COLOR(68, 68, 68, 255), 3.2f * s, 0.0f);
    /* Crimson Saucer Disc (#aa3333) */
    C2D_DrawCircleSolid(sx, sy, 0.0f, 5.8f * s, MAKE_COLOR(170, 51, 51, 255));
    /* Dark Inner Dome (#222222) */
    C2D_DrawCircleSolid(sx, sy - 0.6f * s, 0.0f, 4.4f * s, MAKE_COLOR(34, 34, 34, 255));
    /* Rotating Turret (#444444) + Twin Gun Barrels (#666666) */
    float ca = cosf(turret_ang), sa = sinf(turret_ang);
    float px = -sa * 1.3f * s, py = ca * 1.3f * s;
    float bx = -ca * 5.8f * s, by = -sa * 5.8f * s;
    C2D_DrawLine(sx + px, sy + py, MAKE_COLOR(102, 102, 102, 255),
                 sx + px + bx, sy + py + by, MAKE_COLOR(102, 102, 102, 255), 1.4f * s, 0.0f);
    C2D_DrawLine(sx - px, sy - py, MAKE_COLOR(102, 102, 102, 255),
                 sx - px + bx, sy - py + by, MAKE_COLOR(102, 102, 102, 255), 1.4f * s, 0.0f);
    C2D_DrawCircleSolid(sx, sy, 0.0f, 2.3f * s, MAKE_COLOR(68, 68, 68, 255));
    draw_online_indicator(sx, sy - 9.0f * s, online);
}

/* Authentic Bottom-Dock Vector Icons (`src/data/spr*Icon.js`) */
static void draw_dock_icon(int slot, float cx, float cy, u32 col) {
    /* Base pedestal for weapon platforms (slots 2..6) */
    if (slot >= 2) {
        C2D_DrawRectSolid(cx - 11.0f, cy + 7.0f, 0.0f, 22.0f, 2.2f, col);
        C2D_DrawRectSolid(cx - 7.5f,  cy + 9.5f, 0.0f, 15.0f, 2.0f, col);
    }

    switch (slot) {
        case 0: /* sprSatelliteIcon: 45-deg tilted satellite + planet arc */
            C2D_DrawLine(cx - 8.0f, cy - 8.0f, col, cx + 4.0f, cy + 4.0f, col, 3.5f, 0.0f);
            C2D_DrawLine(cx - 3.0f, cy + 1.0f, col, cx + 1.0f, cy - 3.0f, col, 4.0f, 0.0f);
            draw_circle_outline(cx + 9.0f, cy + 3.0f, 7.5f, 7.5f, 12, col, 1.2f);
            break;
        case 1: /* sprMiningStationIcon: top bar + drill beam + planet arc */
            C2D_DrawRectSolid(cx - 11.0f, cy - 10.0f, 0.0f, 22.0f, 3.5f, col);
            C2D_DrawRectSolid(cx - 4.5f,  cy - 6.0f,  0.0f, 9.0f,  3.5f, col);
            C2D_DrawRectSolid(cx - 1.2f,  cy - 2.5f,  0.0f, 2.4f,  9.5f, col);
            C2D_DrawLine(cx - 5.0f, cy + 3.5f, col, cx, cy + 7.5f, col, 1.3f, 0.0f);
            C2D_DrawLine(cx + 5.0f, cy + 3.5f, col, cx, cy + 7.5f, col, 1.3f, 0.0f);
            draw_circle_outline(cx, cy + 19.0f, 11.0f, 11.0f, 12, col, 1.2f);
            break;
        case 2: /* sprLaserPlatformIcon: turret + twin laser bolts */
            C2D_DrawRectSolid(cx - 5.0f, cy + 4.0f, 0.0f, 10.0f, 3.0f, col);
            C2D_DrawLine(cx - 2.0f, cy + 4.0f, col, cx - 4.0f, cy + 0.5f, col, 1.5f, 0.0f);
            C2D_DrawLine(cx + 2.0f, cy + 4.0f, col, cx,        cy + 0.5f, col, 1.5f, 0.0f);
            C2D_DrawLine(cx - 6.5f, cy - 4.5f, col, cx - 9.0f, cy - 8.5f, col, 1.5f, 0.0f);
            C2D_DrawLine(cx - 2.5f, cy - 1.5f, col, cx - 5.0f, cy - 5.5f, col, 1.5f, 0.0f);
            break;
        case 3: /* sprBeamPlatformIcon: turret + vertical continuous beam */
            C2D_DrawRectSolid(cx - 5.0f, cy + 4.0f, 0.0f, 10.0f, 3.0f, col);
            C2D_DrawRectSolid(cx - 1.5f, cy - 10.5f, 0.0f, 3.0f, 14.5f, col);
            break;
        case 4: /* sprRocketPlatformIcon: vertical missile silhouette */
            C2D_DrawRectSolid(cx - 2.2f, cy - 4.5f, 0.0f, 4.4f, 11.0f, col);
            C2D_DrawLine(cx - 2.2f, cy - 4.5f, col, cx, cy - 9.5f, col, 2.0f, 0.0f);
            C2D_DrawLine(cx + 2.2f, cy - 4.5f, col, cx, cy - 9.5f, col, 2.0f, 0.0f);
            C2D_DrawLine(cx - 5.5f, cy + 6.5f, col, cx - 2.2f, cy + 2.0f, col, 1.8f, 0.0f);
            C2D_DrawLine(cx + 5.5f, cy + 6.5f, col, cx + 2.2f, cy + 2.0f, col, 1.8f, 0.0f);
            break;
        case 5: /* sprEmpPlatformIcon: central emitter sphere + shockwave ring */
            C2D_DrawRectSolid(cx - 1.0f, cy - 1.0f, 0.0f, 2.0f, 8.0f, col);
            C2D_DrawCircleSolid(cx, cy - 1.5f, 0.0f, 3.0f, col);
            draw_circle_outline(cx, cy - 1.5f, 8.5f, 8.5f, 14, col, 1.2f);
            break;
        case 6: /* sprLightningPlatformIcon: emitter sphere + 5 radial zap rays */
            C2D_DrawRectSolid(cx - 1.0f, cy - 1.0f, 0.0f, 2.0f, 8.0f, col);
            C2D_DrawCircleSolid(cx, cy - 1.5f, 0.0f, 3.0f, col);
            C2D_DrawLine(cx, cy - 1.5f, col, cx - 9.5f, cy - 1.5f, col, 1.1f, 0.0f);
            C2D_DrawLine(cx, cy - 1.5f, col, cx + 9.5f, cy - 1.5f, col, 1.1f, 0.0f);
            C2D_DrawLine(cx, cy - 1.5f, col, cx,        cy - 10.5f, col, 1.1f, 0.0f);
            C2D_DrawLine(cx, cy - 1.5f, col, cx - 8.0f, cy - 9.0f, col, 1.1f, 0.0f);
            C2D_DrawLine(cx, cy - 1.5f, col, cx + 8.0f, cy - 9.0f, col, 1.1f, 0.0f);
            break;
    }
}

/* Render Top Screen (400x240) */
static void render_top_screen(void) {
    /* 1. Radial Space Background (`starscape.js`: #000 outer -> #123 mid -> #234 center) */
    C2D_DrawRectSolid(0, 0, 0.0f, TOP_W, TOP_H, MAKE_COLOR(0, 0, 0, 255));
    C2D_DrawCircleSolid(TOP_W * 0.5f, TOP_H * 0.5f, 0.0f, 195.0f, MAKE_COLOR(17, 34, 51, 255));
    C2D_DrawCircleSolid(TOP_W * 0.5f, TOP_H * 0.5f, 0.0f, 95.0f,  MAKE_COLOR(34, 51, 68, 255));

    /* 2. Dual-Layer Parallax Starscape (`starscape.js`) */
    for (int i = 0; i < 240; i++) {
        float parallax = (g_stars[i].layer == 0) ? 0.025f : 0.055f;
        float sx = fmodf(g_stars[i].x - g_cam_x * parallax, TOP_W);
        float sy = fmodf(g_stars[i].y - g_cam_y * parallax, TOP_H);
        if (sx < 0) sx += TOP_W;
        if (sy < 0) sy += TOP_H;
        float sz = (g_stars[i].layer == 0) ? 1.2f : 1.8f;
        C2D_DrawRectSolid(sx, sy, 0.0f, sz, sz, g_stars[i].color);
    }

    /* 3. Dashed Coloured Orbits (`graphics.js`) */
    for (int i = 0; i < g_body_count; i++) {
        Body *b = &g_bodies[i];
        if (!b->active || !b->has_orbit || b->parent_idx < 0) continue;
        if (b->type == BODY_PLANET || b->type == BODY_MOON || i == g_selected_idx) {
            float px, py;
            world_to_top_screen(g_bodies[b->parent_idx].x, g_bodies[b->parent_idx].y, &px, &py);
            float rx = b->orbit_radius * g_zoom;
            float ry = (b->orbit_radius / g_tilt) * g_zoom;
            if (rx > 4.0f && rx < 900.0f) {
                int segs = (b->type == BODY_PLANET) ? 48 : 28;
                u32 ocol = color_with_alpha(b->color, (i == g_selected_idx) ? 160 : 84);
                draw_dashed_orbit(px, py, rx, ry, segs, ocol, 1.2f);
            }
        }
    }

    /* 4. Power Relay Green Network Lines (`orbital.js`) */
    u32 relay_col = MAKE_COLOR(0, 255, 0, 95);
    for (int i = 0; i < g_link_count; i++) {
        Body *a = &g_bodies[g_links[i][0]];
        Body *d = &g_bodies[g_links[i][1]];
        float x1, y1, x2, y2;
        world_to_top_screen(a->x, a->y, &x1, &y1);
        world_to_top_screen(d->x, d->y, &x2, &y2);
        C2D_DrawLine(x1, y1, relay_col, x2, y2, relay_col, 1.3f, 0.0f);
    }

    /* 5. Celestial Bodies, Directional Shadows, and Authentic Station Sprites */
    for (int i = 0; i < g_body_count; i++) {
        Body *b = &g_bodies[i];
        if (!b->active) continue;
        float sx, sy;
        world_to_top_screen(b->x, b->y, &sx, &sy);
        if (sx < -70.0f || sx > TOP_W + 70.0f || sy < -70.0f || sy > TOP_H + 70.0f)
            continue;

        float r = b->size * g_zoom;
        if (r < 2.5f) r = 2.5f;

        if (b->type == BODY_SUN) {
            /* Multi-layer solar corona glow */
            C2D_DrawCircleSolid(sx, sy, 0.0f, r * 1.45f, MAKE_COLOR(255, 220, 40, 45));
            C2D_DrawCircleSolid(sx, sy, 0.0f, r * 1.18f, MAKE_COLOR(255, 240, 80, 95));
            C2D_DrawCircleSolid(sx, sy, 0.0f, r,         MAKE_COLOR(255, 255, 0, 255));
        } else if (b->type == BODY_PLANET || b->type == BODY_MOON) {
            C2D_DrawCircleSolid(sx, sy, 0.0f, r, b->color);
            /* Directional dark shadow away from the Sun at (0,0) (`graphics.js`) */
            float sun_ang = atan2f(b->y, b->x);
            float sh_x = sx + cosf(sun_ang) * (r * 0.32f);
            float sh_y = sy + sinf(sun_ang) * (r * 0.32f);
            C2D_DrawCircleSolid(sh_x, sh_y, 0.0f, r * 0.78f, MAKE_COLOR(0, 0, 0, 145));

            if (i == g_base_idx) {
                draw_online_indicator(sx, sy - r - 6.0f, 1);
                draw_health_bar(sx, sy - r - 13.0f, b->hp, 26.0f);
            }
            if (i == g_selected_idx) {
                draw_circle_outline(sx, sy, r + 3.5f, r + 3.5f, 20, MAKE_COLOR(255, 255, 255, 210), 1.2f);
            }
        } else if (b->type == BODY_SAT) {
            draw_spr_satellite(sx, sy, g_zoom * 1.8f, b->online);
            draw_health_bar(sx, sy - 14.0f, b->hp, 16.0f);
        } else if (b->type == BODY_MINER) {
            if (b->online && b->parent_idx >= 0) {
                float px, py;
                world_to_top_screen(g_bodies[b->parent_idx].x, g_bodies[b->parent_idx].y, &px, &py);
                C2D_DrawLine(sx, sy, MAKE_COLOR(220, 60, 60, 175), px, py, MAKE_COLOR(220, 60, 60, 80), 1.4f, 0.0f);
            }
            draw_spr_mining_station(sx, sy, g_zoom * 1.8f, b->online);
            draw_health_bar(sx, sy - 13.0f, b->hp, 16.0f);
        } else if (b->type == BODY_PLATFORM) {
            /* Active Weapon Visuals (`beamModule.js`, `slowModule.js`, `zapModule.js`) */
            if (b->online && b->has_weapon) {
                if (b->wpn_type == WPN_BEAM && b->beam_active) {
                    float bx, by;
                    world_to_top_screen(b->x - 1000.0f * cosf(b->beam_dir), b->y - 1000.0f * sinf(b->beam_dir), &bx, &by);
                    C2D_DrawLine(sx, sy, MAKE_COLOR(0, 255, 255, 230), bx, by, MAKE_COLOR(0, 255, 255, 55), 3.0f, 0.0f);
                } else if (b->wpn_type == WPN_EMP && b->emp_radius < 400.0f) {
                    draw_circle_outline(sx, sy, b->emp_radius * g_zoom, (b->emp_radius / g_tilt) * g_zoom, 24, MAKE_COLOR(0, 180, 255, 210), 1.8f);
                } else if (b->wpn_type == WPN_LIGHTNING && b->lightning_count > 0) {
                    float lx = sx, ly = sy;
                    for (int k = 0; k < b->lightning_count; k++) {
                        int ei = b->lightning_pts[k];
                        if (g_enemies[ei].active) {
                            float ex, ey;
                            world_to_top_screen(g_enemies[ei].x, g_enemies[ei].y, &ex, &ey);
                            C2D_DrawLine(lx, ly, MAKE_COLOR(0, 255, 255, 255), ex, ey, MAKE_COLOR(255, 255, 0, 255), 1.5f, 0.0f);
                            lx = ex; ly = ey;
                        }
                    }
                }
            }
            draw_spr_defense_platform(sx, sy, g_zoom * 1.9f, b->turret_angle, b->online);
            draw_health_bar(sx, sy - 13.0f, b->hp, 18.0f);
        }
    }

    /* 6. Enemies (`sprEnemyShip.js` & `Enemy.js`: red dart fighters with z*tilt offset) */
    u32 enemy_col = MAKE_COLOR(255, 0, 0, 255);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!g_enemies[i].active) continue;
        float sx, sy;
        world_to_top_screen(g_enemies[i].x, g_enemies[i].y, &sx, &sy);
        sy -= g_enemies[i].z * g_tilt * g_zoom;
        if (sx < -12.0f || sx > TOP_W + 12.0f || sy < -12.0f || sy > TOP_H + 12.0f) continue;

        float ca = cosf(g_enemies[i].angle), sa = sinf(g_enemies[i].angle);
        float nose_x = sx - ca * 4.5f, nose_y = sy - sa * 4.5f;
        float lw_x   = sx + ca * 3.5f - sa * 3.0f, lw_y = sy + sa * 3.5f + ca * 3.0f;
        float rw_x   = sx + ca * 3.5f + sa * 3.0f, rw_y = sy + sa * 3.5f - ca * 3.0f;
        C2D_DrawLine(nose_x, nose_y, enemy_col, lw_x, lw_y, enemy_col, 2.0f, 0.0f);
        C2D_DrawLine(nose_x, nose_y, enemy_col, rw_x, rw_y, enemy_col, 2.0f, 0.0f);
        C2D_DrawLine(lw_x, lw_y, enemy_col, rw_x, rw_y, enemy_col, 1.5f, 0.0f);
    }

    /* 7. Lasers & Explosion Particles */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!g_particles[i].active) continue;
        float sx, sy, ex, ey;
        world_to_top_screen(g_particles[i].x, g_particles[i].y, &sx, &sy);
        world_to_top_screen(g_particles[i].x + 12.0f * cosf(g_particles[i].angle),
                            g_particles[i].y + 12.0f * sinf(g_particles[i].angle), &ex, &ey);
        C2D_DrawLine(sx, sy, g_particles[i].color, ex, ey, g_particles[i].color, 1.5f, 0.0f);
    }

    /* 8. Rockets (`sprRocket.js`: #0F0 triangular missile) */
    u32 rkt_col = MAKE_COLOR(0, 255, 0, 255);
    for (int i = 0; i < MAX_ROCKETS; i++) {
        if (!g_rockets[i].active) continue;
        float sx, sy;
        world_to_top_screen(g_rockets[i].x, g_rockets[i].y, &sx, &sy);
        float ca = cosf(g_rockets[i].angle), sa = sinf(g_rockets[i].angle);
        C2D_DrawLine(sx - ca * 4.5f, sy - sa * 4.5f, rkt_col,
                     sx + ca * 3.5f, sy + sa * 3.5f, MAKE_COLOR(180, 255, 180, 255), 2.4f, 0.0f);
    }

    /* 9. Title Screen Overlay (`loadingscreen.js`: giant glowing "e X o") */
    if (g_state == 0) {
        C2D_DrawRectSolid(0, 0, 0.0f, TOP_W, TOP_H, MAKE_COLOR(0, 0, 0, 120));
        float pulse = 2.0f * sinf((float)g_frame_tick * 0.08f);
        C2D_DrawCircleSolid(TOP_W * 0.5f, 92.0f, 0.0f, 48.0f + pulse, MAKE_COLOR(255, 255, 255, 22));
        C2D_DrawCircleSolid(TOP_W * 0.5f, 92.0f, 0.0f, 28.0f + pulse, MAKE_COLOR(255, 255, 255, 38));
        draw_text("e   o", TOP_W * 0.5f, 58.0f, 1.65f, MAKE_COLOR(255, 255, 255, 255), 1);
        draw_text("X",     TOP_W * 0.5f, 46.0f, 2.35f, MAKE_COLOR(255, 255, 255, 255), 1);

        u8 prompt_a = (u8)(165 + (int)(80.0f * sinf((float)g_frame_tick * 0.07f)));
        draw_text("click or press A to play!", TOP_W * 0.5f, 156.0f, 0.55f, MAKE_COLOR(255, 255, 255, prompt_a), 1);
        draw_text("Original js13k Game by Jack Oatley & Anton K. (ai Doge) | Music by Francis Fonye",
                  TOP_W * 0.5f, 214.0f, 0.38f, MAKE_COLOR(180, 200, 220, 200), 1);
        return;
    }

    /* 10. Placement Ring if building (`Build.js`) */
    if (g_build_active && g_build_parent >= 0 && g_bodies[g_build_parent].active) {
        float px, py;
        world_to_top_screen(g_bodies[g_build_parent].x, g_bodies[g_build_parent].y, &px, &py);
        float rx = g_build_radius * g_zoom;
        float ry = (g_build_radius / g_tilt) * g_zoom;
        u32 bcol = g_build_valid ? MAKE_COLOR(0, 255, 0, 200) : MAKE_COLOR(255, 0, 0, 200);
        draw_circle_outline(px, py, rx, ry, 32, bcol, 2.0f);
    }

    /* World Cursor Reticle */
    float cx, cy;
    world_to_top_screen(g_cursor_wx, g_cursor_wy, &cx, &cy);
    C2D_DrawLine(cx - 5, cy, MAKE_COLOR(255, 255, 255, 180), cx + 5, cy, MAKE_COLOR(255, 255, 255, 180), 1.0f, 0.0f);
    C2D_DrawLine(cx, cy - 5, MAKE_COLOR(255, 255, 255, 180), cx, cy + 5, MAKE_COLOR(255, 255, 255, 180), 1.0f, 0.0f);

    /* 11. Original Exo Top HUD (`gui.js`: rgba(0,0,0,0.5) boxes with white border) */
    draw_exo_box(6.0f, 4.0f, 388.0f, 22.0f, MAKE_COLOR(0, 0, 0, 150), MAKE_COLOR(255, 255, 255, 220), 1.2f);
    char hud_buf[128];
    snprintf(hud_buf, sizeof(hud_buf), "MINERALS: %d   ENERGY: %d   WAVE: %d (NEXT: %ds)   BASE HP: %d%%",
             (int)g_minerals, (int)g_energy, g_wave_num, g_wave_timer_frames / 60,
             (int)g_bodies[g_base_idx].hp);
    draw_text(hud_buf, TOP_W * 0.5f, 8.0f, 0.42f, MAKE_COLOR(255, 255, 255, 255), 1);

    if (g_state == 2) {
        draw_exo_box(125.0f, 96.0f, 150.0f, 40.0f, MAKE_COLOR(0, 0, 0, 200), MAKE_COLOR(255, 255, 255, 255), 2.0f);
        draw_text("PAUSED (Press Y)", TOP_W * 0.5f, 109.0f, 0.52f, MAKE_COLOR(255, 255, 255, 255), 1);
    }

    if (g_status_timer > 0) {
        draw_exo_box(50.0f, TOP_H - 24.0f, 300.0f, 20.0f, MAKE_COLOR(0, 0, 0, 165), MAKE_COLOR(255, 255, 255, 180), 1.0f);
        draw_text(g_status_msg, TOP_W * 0.5f, TOP_H - 20.0f, 0.40f, MAKE_COLOR(255, 255, 255, 255), 1);
    }
}

/* Render Bottom Touch Screen (320x240) - Styled after original Exo `buttons.js` & `gui.js` */
static void render_bottom_screen(void) {
    C2D_DrawRectSolid(0, 0, 0.0f, BOT_W, BOT_H, MAKE_COLOR(6, 10, 16, 255));
    C2D_DrawCircleSolid(BOT_W * 0.5f, BOT_H * 0.5f, 0.0f, 150.0f, MAKE_COLOR(17, 34, 51, 180));

    u32 panel_fill = MAKE_COLOR(0, 0, 0, 155);
    u32 white_rim  = MAKE_COLOR(255, 255, 255, 240);
    u32 dim_rim    = MAKE_COLOR(120, 130, 145, 180);

    /* 1. Interactive System Radar (Left: x=6..166, y=6..166, 160x160) */
    float map_x = 6.0f, map_y = 6.0f, map_sz = 160.0f;
    float map_cx = map_x + map_sz * 0.5f;
    float map_cy = map_y + map_sz * 0.5f;
    float map_scale = (map_sz * 0.45f) / g_system_size;

    draw_exo_box(map_x, map_y, map_sz, map_sz, panel_fill, white_rim, 1.5f);
    for (int i = 0; i < g_body_count; i++) {
        Body *b = &g_bodies[i];
        if (!b->active) continue;
        float mx = map_cx + b->x * map_scale;
        float my = map_cy + b->y * map_scale;
        if (mx < map_x + 2 || mx > map_x + map_sz - 2 || my < map_y + 2 || my > map_y + map_sz - 2) continue;
        if (b->type == BODY_PLANET && b->parent_idx >= 0) {
            float orad = b->orbit_radius * map_scale;
            draw_dashed_orbit(map_cx, map_cy, orad, orad, 24, color_with_alpha(b->color, 70), 1.0f);
        }
        float mr = (b->type == BODY_SUN) ? 4.2f : (b->type == BODY_PLANET ? 2.6f : 1.5f);
        u32 mcol = (i == g_base_idx) ? MAKE_COLOR(0, 255, 0, 255) : b->color;
        C2D_DrawCircleSolid(mx, my, 0.0f, mr, mcol);
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!g_enemies[i].active) continue;
        float mx = map_cx + g_enemies[i].x * map_scale;
        float my = map_cy + g_enemies[i].y * map_scale;
        if (mx >= map_x && mx <= map_x + map_sz && my >= map_y && my <= map_y + map_sz) {
            C2D_DrawRectSolid(mx, my, 0.0f, 1.6f, 1.6f, MAKE_COLOR(255, 0, 0, 255));
        }
    }
    float vx = map_cx + g_cam_x * map_scale;
    float vy = map_cy + g_cam_y * map_scale;
    draw_exo_box(vx - 4.0f, vy - 3.0f, 8.0f, 6.0f, MAKE_COLOR(255, 255, 255, 40), white_rim, 1.0f);
    draw_text("SYSTEM MAP (Tap/Drag)", map_x + 6.0f, map_y + 4.0f, 0.36f, white_rim, 0);

    /* 2. Right Inspector & Upgrade Panel (x=172..314, y=6..166) */
    draw_exo_box(172.0f, 6.0f, 142.0f, 160.0f, panel_fill, white_rim, 1.5f);
    if (g_build_active) {
        draw_text("BUILD ORBITAL", 243.0f, 13.0f, 0.44f, MAKE_COLOR(0, 255, 0, 255), 1);
        draw_text(get_build_name(g_build_type, g_build_wpn), 243.0f, 31.0f, 0.39f, white_rim, 1);
        char cbuf[48];
        snprintf(cbuf, sizeof(cbuf), "Cost: %d Minerals", g_build_cost);
        draw_text(cbuf, 243.0f, 48.0f, 0.38f, MAKE_COLOR(255, 255, 0, 255), 1);
        snprintf(cbuf, sizeof(cbuf), "Orbit Radius: %d", (int)g_build_radius);
        draw_text(cbuf, 243.0f, 65.0f, 0.38f, white_rim, 1);
        draw_text("D-Pad Up/Dn: Orbit", 243.0f, 84.0f, 0.35f, MAKE_COLOR(200, 200, 200, 255), 1);
        draw_text("D-Pad L/R: Planet", 243.0f, 100.0f, 0.35f, MAKE_COLOR(200, 200, 200, 255), 1);

        draw_exo_box(180.0f, 122.0f, 60.0f, 34.0f, MAKE_COLOR(0, 0, 0, 180),
                     g_build_valid ? MAKE_COLOR(0, 255, 0, 255) : dim_rim, 1.5f);
        draw_text("PLACE", 210.0f, 133.0f, 0.40f, g_build_valid ? MAKE_COLOR(0, 255, 0, 255) : dim_rim, 1);

        draw_exo_box(246.0f, 122.0f, 60.0f, 34.0f, MAKE_COLOR(0, 0, 0, 180), MAKE_COLOR(255, 80, 80, 255), 1.5f);
        draw_text("CANCEL", 276.0f, 133.0f, 0.40f, MAKE_COLOR(255, 80, 80, 255), 1);
    } else if (g_selected_idx >= 0 && g_bodies[g_selected_idx].active) {
        Body *b = &g_bodies[g_selected_idx];
        draw_text(b->name, 243.0f, 13.0f, 0.43f, white_rim, 1);
        char ibuf[64];
        snprintf(ibuf, sizeof(ibuf), "HP: %d%% | %s", (int)b->hp, b->online ? "ONLINE" : "OFFLINE");
        draw_text(ibuf, 243.0f, 33.0f, 0.38f, b->online ? MAKE_COLOR(0, 255, 0, 255) : MAKE_COLOR(255, 60, 60, 255), 1);

        if (b->type == BODY_PLATFORM && b->has_weapon) {
            const char *wnames[5] = {"Lasers", "Beam", "Rockets", "EMP", "Lightning"};
            snprintf(ibuf, sizeof(ibuf), "%s (Lv.%d)", wnames[b->wpn_type], b->wpn_level);
            draw_text(ibuf, 243.0f, 52.0f, 0.39f, MAKE_COLOR(255, 255, 0, 255), 1);

            int upg_cost = 75 * b->wpn_level;
            int can_upg = (g_minerals >= (float)upg_cost);
            draw_exo_box(182.0f, 76.0f, 122.0f, 34.0f, MAKE_COLOR(0, 0, 0, 180),
                         can_upg ? white_rim : dim_rim, 1.5f);
            snprintf(ibuf, sizeof(ibuf), "UPGRADE (%dM)", upg_cost);
            draw_text(ibuf, 243.0f, 87.0f, 0.39f, can_upg ? white_rim : dim_rim, 1);
        } else {
            draw_text("X: Center on Base", 243.0f, 62.0f, 0.35f, MAKE_COLOR(200, 210, 220, 255), 1);
            draw_text("L / R: Zoom View", 243.0f, 80.0f, 0.35f, MAKE_COLOR(200, 210, 220, 255), 1);
            draw_text("Y: Pause / Resume", 243.0f, 98.0f, 0.35f, MAKE_COLOR(200, 210, 220, 255), 1);
        }

        draw_exo_box(182.0f, 124.0f, 122.0f, 32.0f, MAKE_COLOR(0, 0, 0, 180), white_rim, 1.5f);
        draw_text(g_music_muted ? "AUDIO: OFF" : "AUDIO: ON", 243.0f, 134.0f, 0.39f, white_rim, 1);
    }

    /* 3. Bottom 7-Button Construction Dock (`src/data/spr*Icon.js`, y=172..234) */
    const int costs[7] = {30, 50, 100, 150, 200, 250, 350};
    for (int i = 0; i < 7; i++) {
        float bx = 6.0f + (float)i * 44.5f;
        float by = 172.0f;
        int can_afford = (g_minerals >= (float)costs[i]);
        u32 rim = can_afford ? white_rim : dim_rim;
        draw_exo_box(bx, by, 41.0f, 62.0f, panel_fill, rim, 1.5f);
        draw_dock_icon(i, bx + 20.5f, by + 22.0f, rim);

        char cstr[16];
        snprintf(cstr, sizeof(cstr), "%d", costs[i]);
        draw_text(cstr, bx + 20.5f, by + 44.0f, 0.36f,
                  can_afford ? MAKE_COLOR(255, 255, 255, 255) : dim_rim, 1);
    }
}

/* Input Handling for 2DS XL / 3DS */
static void handle_input(void) {
    hidScanInput();
    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();

    if (g_state == 0) {
        if (kDown & (KEY_A | KEY_START | KEY_TOUCH)) {
            g_state = 1;
            audio_play_sfx(&g_snd_select, 1);
        }
        return;
    }

    if (kDown & KEY_Y) {
        g_state = (g_state == 1) ? 2 : 1;
        audio_play_sfx(&g_snd_select, 1);
    }

    circlePosition cpad;
    hidCircleRead(&cpad);
    float pan_speed = 12.0f / g_zoom;
    if (abs(cpad.dx) > 25) {
        g_cam_tx += (cpad.dx / 155.0f) * pan_speed;
        g_cursor_wx = g_cam_tx;
    }
    if (abs(cpad.dy) > 25) {
        g_cam_ty -= (cpad.dy / 155.0f) * pan_speed;
        g_cursor_wy = g_cam_ty;
    }

    if (kHeld & KEY_L) g_target_zoom = clampf(g_target_zoom - 0.015f, 0.10f, 1.50f);
    if (kHeld & KEY_R) g_target_zoom = clampf(g_target_zoom + 0.015f, 0.10f, 1.50f);

    if (kDown & KEY_X) {
        g_cam_tx = g_bodies[g_base_idx].x;
        g_cam_ty = g_bodies[g_base_idx].y;
        g_cursor_wx = g_cam_tx;
        g_cursor_wy = g_cam_ty;
        g_selected_idx = g_base_idx;
        set_status("Camera centered on Base Planet");
    }

    if (g_build_active) {
        if (kDown & KEY_DUP)   g_build_radius = clampf(g_build_radius + 100.0f, 100.0f, 800.0f);
        if (kDown & KEY_DDOWN) g_build_radius = clampf(g_build_radius - 100.0f, 100.0f, 800.0f);
        if (kDown & (KEY_DLEFT | KEY_DRIGHT)) {
            for (int step = 1; step <= g_body_count; step++) {
                int idx = (g_build_parent + step) % g_body_count;
                if (g_bodies[idx].active && (g_bodies[idx].type == BODY_SUN || g_bodies[idx].type == BODY_PLANET)) {
                    g_build_parent = idx;
                    g_cam_tx = g_bodies[idx].x;
                    g_cam_ty = g_bodies[idx].y;
                    break;
                }
            }
        }
        if (g_build_parent >= 0 && g_bodies[g_build_parent].active) {
            float max_r = 20.0f * g_bodies[g_build_parent].size;
            if (g_build_radius > max_r) g_build_radius = 100.0f * floorf(max_r / 100.0f);
            if (g_build_radius < 100.0f) g_build_radius = 100.0f;
            g_build_valid = (g_minerals >= (float)g_build_cost);
        } else {
            g_build_valid = 0;
        }

        if (kDown & KEY_B) {
            g_build_active = 0;
            set_status("Construction canceled");
        } else if ((kDown & KEY_A) && g_build_valid) {
            float ang = randf() * TWO_PI_F;
            int new_idx = -1;
            if (g_build_type == BODY_SAT) {
                new_idx = create_body(BODY_SAT, "Satellite", hsl_to_color(160, 100, 50), 3.0f, 0, 0, g_build_parent, g_build_radius, 0.01f, ang);
            } else if (g_build_type == BODY_MINER) {
                new_idx = create_body(BODY_MINER, "Mining Station", hsl_to_color(212, 100, 97), 3.0f, 0, 0, g_build_parent, g_build_radius, 0.01f, ang);
            } else if (g_build_type == BODY_PLATFORM) {
                new_idx = create_body(BODY_PLATFORM, get_build_name(g_build_type, g_build_wpn), hsl_to_color(33, 100, 50), 4.0f, 0, 0, g_build_parent, g_build_radius, 0.01f, ang);
                set_platform_weapon(new_idx, g_build_wpn, 1);
            }
            if (new_idx >= 0) {
                g_minerals -= (float)g_build_cost;
                g_selected_idx = new_idx;
                set_status("Constructed orbital unit!");
                audio_play_sfx(&g_snd_select, 1);
            }
            g_build_active = 0;
        }
    } else {
        if (kDown & KEY_A) {
            int near_idx = find_nearest_body(g_cursor_wx, g_cursor_wy, 0);
            if (near_idx >= 0) {
                g_selected_idx = near_idx;
                g_cam_tx = g_bodies[near_idx].x;
                g_cam_ty = g_bodies[near_idx].y;
                audio_play_sfx(&g_snd_select, 1);
            }
        }
    }

    if (kHeld & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        float tx = (float)touch.px;
        float ty = (float)touch.py;

        if (tx >= 6.0f && tx <= 166.0f && ty >= 6.0f && ty <= 166.0f) {
            float map_cx = 86.0f, map_cy = 86.0f;
            float map_scale = (160.0f * 0.45f) / g_system_size;
            float wx = (tx - map_cx) / map_scale;
            float wy = (ty - map_cy) / map_scale;
            g_cam_tx = wx;
            g_cam_ty = wy;
            g_cursor_wx = wx;
            g_cursor_wy = wy;
            if (kDown & KEY_TOUCH) {
                int near_b = find_nearest_body(wx, wy, g_build_active ? 1 : 0);
                if (near_b >= 0) {
                    if (g_build_active) g_build_parent = near_b;
                    else g_selected_idx = near_b;
                }
            }
        }
    }

    if (kDown & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        float tx = (float)touch.px;
        float ty = (float)touch.py;

        if (ty >= 172.0f && ty <= 234.0f) {
            int slot = (int)((tx - 6.0f) / 44.5f);
            if (slot >= 0 && slot < 7) {
                BodyType btype = (slot == 0) ? BODY_SAT : (slot == 1 ? BODY_MINER : BODY_PLATFORM);
                WeaponType wtype = (slot >= 2) ? (WeaponType)(slot - 2) : WPN_LASER;
                int cost = get_build_cost(btype, wtype);
                if (g_minerals >= (float)cost) {
                    g_build_active = 1;
                    g_build_type = btype;
                    g_build_wpn = wtype;
                    g_build_cost = cost;
                    int p = find_nearest_body(g_cam_x, g_cam_y, 1);
                    g_build_parent = (p >= 0) ? p : g_base_idx;
                    g_build_radius = 100.0f;
                    g_build_valid = 1;
                    set_status("Select parent planet & orbit radius");
                    audio_play_sfx(&g_snd_select, 1);
                } else {
                    set_status("Insufficient minerals!");
                }
            }
        }

        if (g_build_active) {
            if (tx >= 180.0f && tx <= 240.0f && ty >= 122.0f && ty <= 156.0f && g_build_valid) {
                float ang = randf() * TWO_PI_F;
                int new_idx = -1;
                if (g_build_type == BODY_SAT)
                    new_idx = create_body(BODY_SAT, "Satellite", hsl_to_color(160, 100, 50), 3.0f, 0, 0, g_build_parent, g_build_radius, 0.01f, ang);
                else if (g_build_type == BODY_MINER)
                    new_idx = create_body(BODY_MINER, "Mining Station", hsl_to_color(212, 100, 97), 3.0f, 0, 0, g_build_parent, g_build_radius, 0.01f, ang);
                else if (g_build_type == BODY_PLATFORM) {
                    new_idx = create_body(BODY_PLATFORM, get_build_name(g_build_type, g_build_wpn), hsl_to_color(33, 100, 50), 4.0f, 0, 0, g_build_parent, g_build_radius, 0.01f, ang);
                    set_platform_weapon(new_idx, g_build_wpn, 1);
                }
                if (new_idx >= 0) {
                    g_minerals -= (float)g_build_cost;
                    g_selected_idx = new_idx;
                    audio_play_sfx(&g_snd_select, 1);
                }
                g_build_active = 0;
            } else if (tx >= 246.0f && tx <= 306.0f && ty >= 122.0f && ty <= 156.0f) {
                g_build_active = 0;
                audio_play_sfx(&g_snd_select, 1);
            }
        } else {
            if (g_selected_idx >= 0 && g_bodies[g_selected_idx].active) {
                Body *b = &g_bodies[g_selected_idx];
                if (b->type == BODY_PLATFORM && b->has_weapon &&
                    tx >= 182.0f && tx <= 304.0f && ty >= 76.0f && ty <= 110.0f) {
                    int upg_cost = 75 * b->wpn_level;
                    if (g_minerals >= (float)upg_cost) {
                        g_minerals -= (float)upg_cost;
                        b->wpn_level++;
                        set_status("Upgraded weapon platform!");
                        audio_play_sfx(&g_snd_select, 1);
                    } else {
                        set_status("Need more minerals to upgrade!");
                    }
                }
            }
            if (tx >= 182.0f && tx <= 304.0f && ty >= 124.0f && ty <= 156.0f) {
                g_music_muted = !g_music_muted;
                g_sfx_muted = g_music_muted;
                if (g_ndsp_ok) {
                    float mix[12] = {0};
                    mix[0] = g_music_muted ? 0.0f : 0.55f;
                    mix[1] = g_music_muted ? 0.0f : 0.55f;
                    ndspChnSetMix(0, mix);
                }
            }
        }
    }

    g_zoom += (g_target_zoom - g_zoom) * 0.2f;
    g_tilt = clampf(1.0f + (g_zoom - 0.1f), 1.0f, 1.6f);
    g_cam_x += (g_cam_tx - g_cam_x) * 0.2f;
    g_cam_y += (g_cam_ty - g_cam_y) * 0.2f;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    srand(0x3D5E70);

    romfsInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_text_buf = C2D_TextBufNew(2048);

    for (int i = 0; i < 240; i++) {
        g_stars[i].x = randf() * TOP_W;
        g_stars[i].y = randf() * TOP_H;
        g_stars[i].layer = (u8)(i & 1);
        g_stars[i].color = hsl_to_color(randf() * 360.0f, 100.0f, 90.0f);
    }

    audio_init();
    init_solar_system();

    while (aptMainLoop()) {
        int prev_state = g_state;
        handle_input();
        if ((hidKeysDown() & KEY_START) && prev_state != 0) {
            break; /* Clean exit to 3DS HOME Menu */
        }

        update_simulation();

        C2D_TextBufClear(g_text_buf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TargetClear(top, MAKE_COLOR(0, 0, 0, 255));
        C2D_SceneBegin(top);
        render_top_screen();

        C2D_TargetClear(bot, MAKE_COLOR(6, 10, 16, 255));
        C2D_SceneBegin(bot);
        render_bottom_screen();

        C3D_FrameEnd(0);
    }

    audio_cleanup();
    C2D_TextBufDelete(g_text_buf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    return 0;
}
