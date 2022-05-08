#ifndef _INCLUDED_RADIKO_H
#define _INCLUDED_RADIKO_H

#include "freertos/event_groups.h"


#define MAX_STATION_COUNT 20
#define AUTH_DONE_BIT BIT0
#define GOT_STATIONS_BIT BIT1
#define PL_GEN_DONE_BIT BIT2

EventGroupHandle_t radiko_task_group;


char _auth_token[128];
char _playlist_url[128];
char _audio_url[128];
char _region_id[16];

typedef struct
{
    char id[16];
    char name[64];
    char ascii_name[32];
    char logo_xsmall[128];
} station_t;

station_t * stations;
int station_count;

void auth(void* pvParameters);
void get_station_list(void* pvParameters);
void generate_playlist_url(void* pvParameters);
void init_radiko();
#endif