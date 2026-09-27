#include <stdlib.h>
#include <string.h>
#include "TimeParser.h"
#include <ctype.h>

// time format: HHMMSS (6 characters)
int time_parse(char *time) {

	// how many seconds, default returns error
	int seconds = TIME_LEN_ERROR;

	// TODO: Check that string is not null
	if (time == NULL){
		return TIME_ARRAY_ERROR;
	}

	//check that string is 6 characters
	if(strlen(time) != 6){
		return TIME_LEN_ERROR;
	}

	//check that all characters are numbers
	for(int i = 0; i < 6; i++){
		if(!isdigit(time[i])){
			return TIME_CHARACTER_ERROR;
		}
	}
	// Parse values from time string
	// For example: 124033 -> 12hour 40min 33sec
    int values[3];

	values[2] = atoi(time+4); // seconds
	time[4] = 0;

	values[1] = atoi(time+2); // minutes
	time[2] = 0;

	values[0] = atoi(time); // hours
	// Now you have:
	// values[0] hour
	// values[1] minute
	// values[2] second

	// TODO: Add boundary check time values: below zero or above limit not allowed
	// limits are 59 for minutes, 23 for hours, etc
	if (values[0] < 0 || values[0] > 23){
		return TIME_VALUE_ERROR;
	}

	if (values [1] < 0 || values[1] > 59){
		return TIME_VALUE_ERROR;
	}

	if (values[2] < 0 || values[2] > 59){
		return TIME_VALUE_ERROR;
	}

	// TODO: Calculate return value from the parsed minutes and seconds
	// Otherwise error will be returned!
	// seconds = ...
	seconds = values[1] * 60 + values[2];

	if(seconds == 0){
		return TIME_ZERO_ERROR;
	}

	return seconds;
}

int parse_traffic_light_sequence(char *input, TrafficLightCommand *commands, int max_size){
	if(input == NULL || commands == NULL){
		return TRAFFIC_LIGHT_ERROR;
	}

	if(max_size <= 0){
		return TRAFFIC_LIGHT_ERROR;
	}

	int count = 0;
	char *pair = strtok(input, ":");

	while(pair != NULL){
		if(count >= max_size){
			return TRAFFIC_LIGHT_ERROR;
		}
		//command must be at least R,1
		if(strlen(pair) < 3){
			return TRAFFIC_LIGHT_ERROR;
		}
		//First character must R,Y or G
		if(pair[0] != 'R' &&
		   pair[0] != 'Y' &&
		   pair[0] != 'G'){
			return TRAFFIC_LIGHT_COLOR_ERROR;
		   }
		//second character mut be ,
		if(pair[1] != ','){
			return TRAFFIC_LIGHT_ERROR;
		}   
		// characters after , must be numbers
		for(int i = 2; pair[i] != '\0'; i++){
			if(!isdigit(pair[i])){
				return TRAFFIC_LIGHT_TIME_ERROR;
			}
		}
		int time = atoi(pair + 2);
		//time must be positive
		if(time <= 0){
			return TRAFFIC_LIGHT_TIME_ERROR;
		}
		commands[count].color = pair[0];
		commands[count].time = time;

		count++;
		
		pair = strtok(NULL, ":");
	}

	if(count == 0){
		return TRAFFIC_LIGHT_ERROR;
	}
	return count;
}	