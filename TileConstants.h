#ifdef TILE_CONSTANTS_HEADER_GUARD
#else
#define TILE_CONSTANTS_HEADER_GUARD

#define TILE_SIZE_IN_PIXELS 32
#define MAX_PARTICLE_PER_TILE 1024
#define COLLECT_PARTICLE_COUNT_PER_THREAD 1 // Should be a divisor of MAX_PARTICLE_PER_TILE

#define DISABLE_ROTATION
//#define DEBUG_SORTING
#define INTERLEAVED_PARTICLE_COLLECTION

#endif