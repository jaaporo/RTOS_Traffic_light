#ifndef TIMEPARSER_H
#define TIMEPARSER_H

// Error codes
#define TIME_LEN_ERROR      -1
#define TIME_ARRAY_ERROR    -2
#define TIME_VALUE_ERROR    -3

#define TIME_ZERO_ERROR     -4
#define TIME_CHARACTER_ERROR -5
#define TRAFFIC_LIGHT_ERROR -6
#define TRAFFIC_LIGHT_COLOR_ERROR -7
#define TRAFFIC_LIGHT_TIME_ERROR -8

using namespace std;

struct TrafficLightCommand{
    char color; 
    int time;
};

int time_parse(char *time);

int parse_traffic_light_sequence(char *input, TrafficLightCommand *commands, int max_size);

#endif
