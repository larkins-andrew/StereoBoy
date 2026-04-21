#ifndef CORE1_ENTRY
#define CORE1_ENTRY

void dprint(char * str, ...);

void core1_entry();

void update_scope_core1();
static void process_audio_batch();
void addIcons(uint16_t* frame_buffer, bool enabled);

extern volatile uint16_t potVal;

#endif