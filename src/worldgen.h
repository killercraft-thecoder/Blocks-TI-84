#pragma once
#include <sys/util.h>
#include "world.h"
#include "player.h"

#define WATER_LEVEL 5

// Slightly finer grid gives smoother hills without too much cost
#define GRID_STEP 6
#define GRID_OFFSET (GRID_STEP / 2)
#define GRID_SIZE ((WORLD_SIZE / GRID_STEP) + 2)

// Makes a "natural" looking world with randomly generated terrain and trees
void generate_natural(world_t &world, player_t &player) {
    // ---------------------------------------------------------------------
    // Base layers: bedrock + water volume
    // ---------------------------------------------------------------------
    world.fill_space(0, 0, 0,
                     WORLD_SIZE - 1, 0, WORLD_SIZE - 1,
                     BEDROCK);

    world.fill_space(0, 1, 0,
                     WORLD_SIZE - 1, WATER_LEVEL, WORLD_SIZE - 1,
                     WATER);

    // ---------------------------------------------------------------------
    // Coarse heightmap grid (cheaper, smoother, more varied)
    // ---------------------------------------------------------------------
    uint8_t grid[GRID_SIZE][GRID_SIZE];

    for(uint8_t gx = 0; gx < GRID_SIZE; gx++) {
        for(uint8_t gz = 0; gz < GRID_SIZE; gz++) {
            // Two-layer variation: base hills + larger-scale offset
            uint8_t low  = randInt(3, 10);
            uint8_t high = randInt(0, 4);

            grid[gx][gz] = low + high;   // 3..14
        }
    }

    const uint16_t denom = (uint16_t)GRID_STEP * (uint16_t)GRID_STEP;

    // ---------------------------------------------------------------------
    // Terrain: bilinear integer interpolation
    // ---------------------------------------------------------------------
    for(uint8_t x = 0; x < WORLD_SIZE; x++) {
        uint8_t grid_x = x / GRID_STEP;
        uint8_t lerp_x = x - (grid_x * GRID_STEP);

        for(uint8_t z = 0; z < WORLD_SIZE; z++) {
            uint8_t grid_z = z / GRID_STEP;
            uint8_t lerp_z = z - (grid_z * GRID_STEP);

            // Clamp indices to valid range to avoid overrun at the edge
            uint8_t gx0 = (grid_x < GRID_SIZE - 1) ? grid_x     : GRID_SIZE - 2;
            uint8_t gz0 = (grid_z < GRID_SIZE - 1) ? grid_z     : GRID_SIZE - 2;
            uint8_t gx1 = gx0 + 1;
            uint8_t gz1 = gz0 + 1;

            uint8_t h00 = grid[gx0][gz0];
            uint8_t h10 = grid[gx1][gz0];
            uint8_t h01 = grid[gx0][gz1];
            uint8_t h11 = grid[gx1][gz1];

            uint8_t inv_x = GRID_STEP - lerp_x;
            uint8_t inv_z = GRID_STEP - lerp_z;

            uint16_t height =
                (uint16_t)h00 * inv_x * inv_z +
                (uint16_t)h10 * lerp_x * inv_z +
                (uint16_t)h01 * inv_x * lerp_z +
                (uint16_t)h11 * lerp_x * lerp_z;

            height /= denom;

            // Slight bias to create more hills above water
            if(height > 0 && height < WATER_LEVEL + 2) {
                height += 1;
            }

            if(height > 3) {
                uint8_t stone_top = (height > 3) ? (height - 3) : 1;
                world.fill_space(x, 1, z,
                                 x, stone_top, z,
                                 STONE);

                uint8_t dirt_top = (height > WORLD_HEIGHT - 1) ? (WORLD_HEIGHT - 1) : height;
                uint8_t dirt_bottom = (stone_top + 1 <= dirt_top) ? stone_top + 1 : dirt_top;

                if(dirt_bottom <= dirt_top) {
                    world.fill_space(x, dirt_bottom, z,
                                     x, dirt_top, z,
                                     DIRT);
                }
            } else {
                uint8_t dirt_top = (height > WORLD_HEIGHT - 1) ? (WORLD_HEIGHT - 1) : height;
                if(dirt_top >= 1) {
                    world.fill_space(x, 1, z,
                                     x, dirt_top, z,
                                     DIRT);
                }
            }

            // Grass only where above or at water level and exposed
            if(height >= WATER_LEVEL && height < WORLD_HEIGHT) {
                world.fill_space(x, height, z,
                                 x, height, z,
                                 GRASS);
            }
        }
    }

    // ---------------------------------------------------------------------
    // Faster shoreline: 2D neighbor check instead of full 3D radius
    // ---------------------------------------------------------------------
    for(uint8_t y = WATER_LEVEL - 1; y <= WATER_LEVEL; y++) {
        if(y >= WORLD_HEIGHT) break;

        for(uint8_t x = 0; x < WORLD_SIZE; x++) {
            for(uint8_t z = 0; z < WORLD_SIZE; z++) {
                uint8_t id = world.blocks[y][x][z];

                if(id != GRASS && id != DIRT) continue;
                if(y >= WORLD_HEIGHT - 1) continue;

                uint8_t above = world.blocks[y + 1][x][z];
                if(above != AIR && above != WATER) continue;

                bool near_water = false;

                // 2D 3x3 neighborhood is enough for a natural beach look
                for(int8_t bx = -2; bx <= 2 && !near_water; bx++) {
                    int8_t nx = (int8_t)x + bx;
                    if(nx < 0 || nx >= WORLD_SIZE) continue;

                    for(int8_t bz = -2; bz <= 2; bz++) {
                        int8_t nz = (int8_t)z + bz;
                        if(nz < 0 || nz >= WORLD_SIZE) continue;

                        if(world.blocks[y][nx][nz] == WATER) {
                            near_water = true;
                            break;
                        }
                    }
                }

                if(near_water) {
                    world.blocks[y][x][z] = SAND;
                }
            }
        }
    }

    // ---------------------------------------------------------------------
    // Trees (small tweak: avoid scanning all the way down each time)
    // ---------------------------------------------------------------------
    const uint8_t tree_cnt = 14; // a bit denser forests
    for(uint8_t i = 0; i < tree_cnt; i++) {
        uint8_t x = 2 + (random() % (WORLD_SIZE - 1 - 4));
        uint8_t z = 2 + (random() % (WORLD_SIZE - 1 - 4));

        int8_t y = WORLD_HEIGHT - 2;

        // Find the first non-air block going down
        while(y > 1 && world.blocks[y][x][z] == AIR) {
            y--;
        }

        if(world.blocks[y][x][z] != GRASS) continue;
        if(world.blocks[y + 1][x][z] != AIR) continue;

        world.add_tree(x, y + 1, z);
        world.blocks[y][x][z] = DIRT;
    }

    // ---------------------------------------------------------------------
    // Ores (same logic, slightly cheaper random use)
    // ---------------------------------------------------------------------
    for(uint8_t y = 1; y < WORLD_HEIGHT; y++) {
        for(uint8_t x = 0; x < WORLD_SIZE; x++) {
            for(uint8_t z = 0; z < WORLD_SIZE; z++) {
                if(world.blocks[y][x][z] != STONE) continue;

                uint8_t r = (uint8_t)randInt(0, 19);

                if(r == 0) {
                    world.blocks[y][x][z] = COAL_ORE;
                } else if(r == 1) {
                    world.blocks[y][x][z] = IRON_ORE;
                }
            }
        }
    }

    // ---------------------------------------------------------------------
    // Place player at center, topmost air block
    // ---------------------------------------------------------------------
    player.x = WORLD_SIZE / 2;
    player.z = WORLD_SIZE / 2;
    player.y = 0;

    while(player.y <= (WORLD_HEIGHT - 1)) {
        if(world.blocks[player.y][player.x][player.z] == AIR) break;
        player.y++;
    }
}