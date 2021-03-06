#include "process.h"
#include "MessageReader.h"
#include "MessageWriter.h"
#include "lights.h"
#include <Arduino.h>

#define uint16touint8(value, byteArray, index) {byteArray[index + 1] = value & 0xFF; byteArray[index] = (value & 0xFF00) >>8;}

uint8_t strip_increament[] = {0, 0, 0, 0, 0, 0, 0, 0};

struct team_t team_data[NUM_TEAMS];
struct motor_t motors[NUM_MOTORS];
struct pole_t poles[8];

uint8_t active = 0;
bool invalidTeamConfigure;

void process_message(struct message_t *message) {
	uint8_t body_length;

	if (message->state != MESSAGE_READY) {
		return;
	}

	//Serial.print(message->data.header.length);
	//Serial.print(": ");

	body_length = message->data.header.length - sizeof(message->data.header);

	//Serial.print(message->data.header.action);
	//Serial.print(" ");

	switch (message->data.header.action) {
		case 'm':
			if (process_motor_message((struct motor_message_t *)&message->data.body, body_length)) {
				message_processed(message);
			}
			break;

		case 'k':
			break;

		case 'p':
			if(process_ping_message()){
				message_processed(message);
			}
			break;
		case 's':
			if (process_start_message((struct start_message_t *)&message->data.body, body_length)) {
				message_processed(message);
			}
			break;
		case 't':
			if (process_team_message((struct team_message_t *)&message->data.body, body_length)){
				message_processed(message);
			}
		default:
			message_processed(message);
			break;
	}
}

uint8_t process_ping_message(){
	struct message_output_t outputMessage{};
	uint8_t body[MAX_MESSAGE_SIZE - 2];
	body[0] = 1;
	writerPrepMessage(&outputMessage, 'p', body);
	writerSendMessage(&outputMessage);
	return 1;

}

uint8_t process_team_message(struct team_message_t *team_message, uint8_t size){
		struct message_output_t outputMessage{};
		uint8_t data[MAX_MESSAGE_SIZE - 2];
		//Serial.println(team_message->readWrite);
        if (!team_message->readWrite) {
            data[1] = team_data[team_message->team].active;
            data[0] = team_data[team_message->team].color;
            //uint16touint8(team_data[team_message->team].score, data, 2);
            data[2] = team_data[team_message->team].score;
            data[3] = team_data[team_message->team].redBall;
            data[4] = team_data[team_message->team].greenBall;
            data[5] = team_data[team_message->team].blueBall;
            data[6] = team_data[team_message->team].purpleBall;
            data[7] = team_data[team_message->team].racketBall;
			data[8] = team_data[team_message->team].readWrite;
            writerPrepMessage(&outputMessage, 't', data);
            writerSendMessage(&outputMessage);
        }
        else {
            if (active) {
                PORTC |= (1 << (team_message->team * 2 + 1));
            }
            if ((team_message->readWrite >> 7) && 0x01) {
                team_data[team_message->team].racketBall = team_message->racketBall;
            }
            if ((team_message->readWrite >> 6) && 0x01) {
                team_data[team_message->team].purpleBall = team_message->purpleBall;
            }
            if ((team_message->readWrite >> 5) && 0x01) {
                team_data[team_message->team].blueBall = team_message->blueBall;
            }
            if ((team_message->readWrite >> 4) && 0x01) {
                team_data[team_message->team].greenBall = team_message->greenBall;
            }
            if ((team_message->readWrite >> 3) && 0x01) {
                team_data[team_message->team].redBall = team_message->redBall;
            }
            if ((team_message->readWrite >> 2) && 0x01) {
                team_data[team_message->team].score = team_message->score;
            }
            PORTC &= ~(1 << (team_message->team*2 + 1));
        }

	return 1;
}

void process_begin(){
	uint8_t i;
	for(i=0; i<8; i++){
		poles[i].integrator = DEBOUNCE_MAX;
		poles[i].lastUpdate = 0;
	}
}


uint8_t process_start_message(struct start_message_t *start_message, uint8_t size){
    uint8_t i;
    uint8_t numTeams = 0;
    if (start_message->setupBit){
        for (i=0; i<4; i++){
            if (start_message->teams&(1<<i)){
                team_data[i].active = 1;
                team_data[i].color = i;
                team_data[i].score = 0;
                team_data[i].readWrite = 0;
                struct message_output_t outputMessage{};
                uint8_t body[MAX_MESSAGE_SIZE - 2];
                body[0] = (uint8_t) (1 << (i*2));
                writerPrepMessage(&outputMessage, 'p', body);
                writerSendMessage(&outputMessage);

                numTeams++;
            }
            else{
                team_data[i].active = 0;
            }
        }
        switch (numTeams){
            case 2:
            case 4: {
                invalidTeamConfigure = false;
                break;
            }
            case 3:{
                invalidTeamConfigure = false;
                break;
            }
            case 0:
            case 1:{
                invalidTeamConfigure = true;
            }
        }
	}

	if (start_message->start){
		if (invalidTeamConfigure){
				Serial.println("Invalid Team Configuration Error!");
		}
		else{
            for (i=0; i<4; i++) {
                if (team_data[i].active)
                    PORTC |= (1 << (i * 2));
                else
                    PORTC &= ~(1 << (i * 2));

                active = 1;
            }
		}
	}
	else{
        PORTC = 0;
		active = 0;
	}

		return 1;
}

uint8_t process_motor_message(struct motor_message_t *motor_message, uint8_t size) {
	uint8_t motor_number = motor_message->motor_number;

	//Serial.println(motor_number);
	//Serial.print(" ");

	if (!(0 <= motor_number && motor_number < NUM_MOTORS)) {
		return 1;
	}
	motors[motor_number].value = motor_message->value;

	return 1;
}

static uint8_t debounce(uint16_t portRegister, uint8_t port, uint8_t poleId){

  uint8_t output = 0;

  if (!(portRegister&(1<<port))){
    if (poles[poleId].integrator > 0)
      poles[poleId].integrator--;
  }
  else if (poles[poleId].integrator < DEBOUNCE_MAX)
      poles[poleId].integrator++;

  if (poles[poleId].integrator == 0)
      output = 0;
  else if (poles[poleId].integrator >= DEBOUNCE_MAX){
    output = 1;
    poles[poleId].integrator = DEBOUNCE_MAX;
  }

  return output;
}

ISR(TIMER3_COMPA_vect){
	//Serial.println(millis());
	if (active){
	}
}

ISR(TIMER4_COMPA_vect){
  int i, j;
  for(i=0; i<NUMBER_OF_STRIPS; i++){
    if (lightsGetFlash(i)){
      for(j=0; j<LIGHTS_PER_STRIP; j++){
        if (j <= strip_increament[i] || j >= strip_increament[i] + 8){
            lightsSetColor(j, BLACK, i);

        }
        else {
            lightsSetColor(j, (color_t)poles[i].colorOwnership, i);
        }
      }
    }
  }
}
