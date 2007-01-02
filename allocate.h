void checkIntegrity(void);
void checkLeak(void);
void *debugMalloc(unsigned int length, int tag);
void *debugRealloc(void *memory, unsigned int length, int tag);
void debugFree(void *memoryParameter, int tag);
