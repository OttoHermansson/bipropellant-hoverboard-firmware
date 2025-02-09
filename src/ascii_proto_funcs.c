/*
* This file is part of the hoverboard-firmware-hack project.
*
* Copyright (C) 2018 Simon Hailes <btsimonh@googlemail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stm32f1xx_hal.h"
#include "defines.h"
#include "config.h"


#ifdef CONTROL_SENSOR
    #include "sensorcoms.h"
#endif
#include "protocolfunctions.h"
#ifdef HALL_INTERRUPTS
    #include "hallinterrupts.h"
#endif
#ifdef SOFTWARE_SERIAL
    #include "softwareserial.h"
#endif
#include "bldc.h"
#ifdef FLASH_STORAGE
    #include "flashcontent.h"
    #include "flashaccess.h"
#endif
#include "comms.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "control_structures.h"


//////////////////////////////////////////////////////////
// from protocol.c
extern int control_type;
//////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////
//
// ASCII protocol:
// this accepts command sup to 10 bytes long terminated with CR.
// one of these commands (I) can enable an 'immediate' mode.
// In 'immediate' mode, keypresses cause immediate action;
// for example, controlling speed, or getting real-time feedback.
//
//////////////////////////////////////////////////////////


///////////////////////////////////////////////
// extern variables you want to read/write here
#ifdef CONTROL_SENSOR
extern SENSOR_DATA sensor_data;
extern int sensor_control;
extern int sensor_stabilise;
#endif

#ifdef FLASH_STORAGE
// from main.c
extern void change_PID_constants();
#endif

extern uint8_t enable; // global variable for motor enable
extern volatile uint32_t input_timeout_counter; // global variable for input_timeout
extern int dspeeds[2];
extern int pwms[2];



extern uint8_t debug_out;
extern uint8_t disablepoweroff;
extern int powerofftimer;
extern uint8_t buzzerFreq;    // global variable for the buzzer pitch. can be 1, 2, 3, 4, 5, 6, 7...
extern uint8_t buzzerPattern; // global variable for the buzzer pattern. can be 1, 2, 3, 4, 5, 6, 7...
extern uint16_t buzzerLen;
extern uint8_t enablescope; // enable scope on values

static int speedB = 0;
static int steerB = 0;


static char *control_types[]={
    "none",
    "Position",
    "Speed (disabled)",
    "PWM Direct"
};

///////////////////////////////////////////////


extern int protocol_post(PROTOCOL_STAT *s, PROTOCOL_MSG2 *msg);



int immediate_dir(PROTOCOL_STAT *s, char byte, char *ascii_out) {
    int dir = 1;
    switch(byte){
        case 'S':
        case 's':
            dir = -1;
        case 'W':
        case 'w':
            if (!enable) { speedB = 0; steerB = 0; }
            enable = 1;
            input_timeout_counter = 0;

            switch (control_type){
#ifdef FLASH_STORAGE
                case CONTROL_TYPE_POSITION:
    #ifdef HALL_INTERRUPTS
                    PosnData.wanted_posn_mm += dir * 100;
                    sprintf(ascii_out, "wanted_posn now %ldmm\r\n", PosnData.wanted_posn_mm);
    #endif
                    break;
                case CONTROL_TYPE_SPEED:
                    speedB += 10*dir;
                    SpeedData.wanted_speed_mm_per_sec = CLAMP(speedB * SPEED_COEFFICIENT -  steerB * STEER_COEFFICIENT, -1000, 1000);
                    sprintf(ascii_out, "speed now %d, steer now %d, speed %ld\r\n", speedB, steerB, SpeedData.wanted_speed_mm_per_sec);
                    break;
#endif
                case CONTROL_TYPE_PWM:
                    speedB += 10*dir;
                    PWMData.pwm = CLAMP(speedB * SPEED_COEFFICIENT -  steerB * STEER_COEFFICIENT, -1000, 1000);
                    sprintf(ascii_out, "speed now %d, steer now %d, pwm %ld\r\n", speedB, steerB, PWMData.pwm);
                    break;
            }
            break;

        case 'A':
        case 'a':
            dir = -1;
        case 'D':
        case 'd':
            if (!enable) { speedB = 0; steerB = 0; }
            enable = 1;
            input_timeout_counter = 0;
            switch (control_type){
#ifdef FLASH_STORAGE
                case CONTROL_TYPE_POSITION:
    #ifdef HALL_INTERRUPTS
                    PosnData.wanted_posn_mm -= dir * 100;
                    sprintf(ascii_out, "wanted_posn now %ldmm\r\n", PosnData.wanted_posn_mm);
    #endif
                    break;
                case CONTROL_TYPE_SPEED:
                    steerB += 10*dir;
                    SpeedData.wanted_speed_mm_per_sec = CLAMP(speedB * SPEED_COEFFICIENT -  steerB * STEER_COEFFICIENT, -1000, 1000);
                    sprintf(ascii_out, "speed now %d, steer now %d, speed %ld\r\n", speedB, steerB, SpeedData.wanted_speed_mm_per_sec);
                    break;
#endif
                case CONTROL_TYPE_PWM:
                    steerB += 10*dir;
                    PWMData.pwm = CLAMP(speedB * SPEED_COEFFICIENT -  steerB * STEER_COEFFICIENT, -1000, 1000);
                    sprintf(ascii_out, "speed now %d, steer now %d, pwm %d->%ld\r\n", speedB, steerB, pwms[0], PWMData.pwm);
                    break;
            }
            break;
    }

    return 1;
}


int immediate_stop(PROTOCOL_STAT *s, char byte, char *ascii_out) {
    speedB = 0;
    steerB = 0;
    PWMData.pwm = CLAMP(speedB * SPEED_COEFFICIENT +  steerB * STEER_COEFFICIENT, -1000, 1000);
    SpeedData.wanted_speed_mm_per_sec = speedB;
#ifdef HALL_INTERRUPTS
    HallData[0].HallSpeed_mm_per_s = HallData[1].HallSpeed_mm_per_s = 0;
#endif
    dspeeds[0] = dspeeds[1] = speedB;
    pwms[0] = pwms[1] = speedB;
#ifdef HALL_INTERRUPTS
    PosnData.wanted_posn_mm = HallData[0].HallPosn_mm;
#endif
#ifdef CONTROL_SENSOR
    sensor_control = 0;
#endif
    enable = 0;
    sprintf(ascii_out, "Stop set\r\n");
    return 1;
}

int immediate_quit(PROTOCOL_STAT *s, char byte, char *ascii_out) {
    enable_immediate = 0;
    speedB = 0;
    steerB = 0;
    PWMData.pwm = CLAMP(speedB * SPEED_COEFFICIENT -  steerB * STEER_COEFFICIENT, -1000, 1000);
    SpeedData.wanted_speed_mm_per_sec = speedB;
#ifdef HALL_INTERRUPTS
    HallData[0].HallSpeed_mm_per_s = HallData[0].HallSpeed_mm_per_s = 0;
#endif
    pwms[0] = pwms[1] = speedB;
#ifdef HALL_INTERRUPTS
    PosnData.wanted_posn_mm = HallData[0].HallPosn_mm;
#endif
#ifdef CONTROL_SENSOR
    sensor_control = 0;
#endif
    control_type = 0;
    enable = 0;
    sprintf(ascii_out, "Immediate commands disabled\r\n");
    return 1;
}

int immediate_hall(PROTOCOL_STAT *s, char byte, char *ascii_out) {
#ifdef HALL_INTERRUPTS
    sprintf(ascii_out,
        "L: P:%ld(%ldmm) S:%ld(%ldmm/s) dT:%lu Skip:%lu Dma:%d\r\n"\
        "R: P:%ld(%ldmm) S:%ld(%ldmm/s) dT:%lu Skip:%lu Dma:%d\r\n",
        HallData[0].HallPosn, HallData[0].HallPosn_mm, HallData[0].HallSpeed, HallData[0].HallSpeed_mm_per_s, HallData[0].HallTimeDiff, HallData[0].HallSkipped, local_hall_params[0].dmacount,
        HallData[1].HallPosn, HallData[1].HallPosn_mm, HallData[1].HallSpeed, HallData[1].HallSpeed_mm_per_s, HallData[1].HallTimeDiff, HallData[1].HallSkipped, local_hall_params[1].dmacount
    );
#else
    sprintf(ascii_out, "Hall Data not available\r\n");
#endif
    return 1;
}

int immediate_sensors(PROTOCOL_STAT *s, char byte, char *ascii_out) {
#ifdef CONTROL_SENSOR
    sprintf(ascii_out,
        "L: OK:%d Foot:%d Angle:%d Roll:%d Accel:%d\r\n",
        sensor_data.sensor_ok, (sensor_data.complete.AA_55 == 0x55)?1:0, sensor_data.complete.Angle, sensor_data.complete.Roll, sensor_data.complete.Accelleration
    );
#else
    sprintf(ascii_out, "Sensor Data not available\r\n");
#endif
    return 1;
}

int immediate_electrical(PROTOCOL_STAT *s, char byte, char *ascii_out) {
    sprintf(ascii_out,
        "Bat: %dmV(%d) Temp:%dC(%d)\r\n"
        "L: Current:%dmA Avg:%dmA r1:%d r2:%d\r\n"\
        "R: Current:%dmA Avg:%dmA r1:%d r2:%d\r\n",
        (int)(electrical_measurements.batteryVoltage*1000), electrical_measurements.bat_raw,
        (int)electrical_measurements.board_temp_deg_c, electrical_measurements.board_temp_raw,
        (int)(electrical_measurements.motors[0].dcAmps*1000.0), (int)(electrical_measurements.motors[0].dcAmpsAvg*1000.0), electrical_measurements.motors[0].r1, electrical_measurements.motors[0].r2,
        (int)(electrical_measurements.motors[1].dcAmps*1000.0), (int)(electrical_measurements.motors[1].dcAmpsAvg*1000.0), electrical_measurements.motors[1].r1, electrical_measurements.motors[1].r2
    );
    return 1;
}

int immediate_stm32(PROTOCOL_STAT *s, char byte, char *ascii_out) {
#ifndef SKIP_STM32SPECIFIC
//        case 'G':
    sprintf(ascii_out,
        "A:%04X B:%04X C:%04X D:%04X E:%04X\r\n"\
        "Button: %d Charge:%d\r\n",
        (int)GPIOA->IDR, (int)GPIOB->IDR, (int)GPIOC->IDR, (int)GPIOD->IDR, (int)GPIOE->IDR,
        (int)(BUTTON_PORT->IDR & BUTTON_PIN)?1:0,
        (int)(CHARGER_PORT->IDR & CHARGER_PIN)?1:0
    );
#endif
    return 1;
}

int immediate_toggle_control(PROTOCOL_STAT *s, char byte, char *ascii_out) {
//case 'O':
    int control_old = control_type;
    //stop all
    immediate_stop(s, byte, ascii_out);
    ascii_out += strlen(ascii_out);
    control_type = (control_old+1) % CONTROL_TYPE_MAX;
    sprintf(ascii_out, "control type now %d (%s)\r\n", control_type, control_types[control_type]);
    return 1;
}

int line_set_alarm(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
//case 'A':
    int a = 0;
    int b = 0;
    int c = 0;
    if (strlen(cmd) > 1){
        sscanf(cmd+1, "%d %d %d", &a, &b, &c);
    }
    if (a && (0==c)){
        c = 1000;
    }

    buzzerFreq = a;
    buzzerPattern = b;
    buzzerLen = c/5; // roughly 5ms per main loop, so 1s default
    sprintf(ascii_out, "Alarm set to %d %d %d\r\n", a, b, c);
    return 1;
}

int line_toggle_sensor_control(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
#ifdef CONTROL_SENSOR
//case 'B':
    sensor_control ^= 1;
    control_type = 0;
    speedB = 0;
    steerB = 0;
    SpeedData.wanted_speed_mm_per_sec = speedB;
#ifdef HALL_INTERRUPTS
    PosnData.wanted_posn_mm = HallData[0].HallPosn_mm;
#endif
    sprintf(ascii_out, "Sensor control now %d\r\n", sensor_control);
#endif
    return 1;
}


int line_electrical(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
//case 'c':
    immediate_electrical(s, *cmd, ascii_out);
    return 1;
}

int line_main_timing_stats(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
#ifdef HALL_INTERRUPTS
//case 's': // display stats from main timing
    // we don't have float printing
    sprintf(ascii_out, "Main loop interval_us %d; lates %d, processing_us %d\r\n",
        (int)(timeStats.main_interval_ms * 1000), timeStats.main_late_count, (int)(timeStats.main_processing_ms*1000));
#endif
    return 1;
}


int line_debug_control(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
//case 'E':
    if (strlen(cmd) == 1){
        debug_out = 0;
        enablescope = 0;
    } else {
        if ((cmd[1] | 0x20) == 's'){
            enablescope = 1;
            debug_out = 1;
        }
        if ((cmd[1] | 0x20) == 'c'){
            debug_out = 1;
        }
    }
    sprintf(ascii_out, "debug_out now %d\r\nenablescope now %d\r\n", debug_out, enablescope);
    return 1;
}



// NOTE: needs params
int line_generic_var(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
//case 'f': // setting any parameter marked with uistr
    int len = strlen(cmd);
    if (len == 1){
        sprintf(ascii_out, "no flash var given\r\n");
    } else {
        if ((cmd[1] | 0x20) == 'i'){ // initilaise
#ifdef FLASH_STORAGE
            memset(&FlashContent, 0, sizeof(FlashContent));
            memcpy(&FlashContent, &FlashDefaults, (sizeof(FlashContent) < sizeof(FlashDefaults))?sizeof(FlashContent) : sizeof(FlashDefaults)) ;
            writeFlash( (unsigned char *)&FlashContent, sizeof(FlashContent) );
            sprintf(ascii_out, "Flash initialised\r\n");
#else
            sprintf(ascii_out, "Flash not implemented\r\n");
#endif
        } else {
            if ((cmd[1] | 0x20) == 'a'){
                // read all
                for (int i = 0; i < (sizeof(params)/sizeof(params[0])); i++){
                    if(params[i] != NULL) {
                        if (params[i]->uistr){
                            switch (params[i]->ui_type){
                                case UI_SHORT:
                                    // read it
                                    if (params[i]->fn) params[i]->fn( s, params[i], FN_TYPE_PRE_READ, NULL, 0 );
                                    sprintf(ascii_out, "%s(%s): %d\r\n",
                                            (params[i]->description)?params[i]->description:"",
                                            params[i]->uistr,
                                            (int)*(short *)params[i]->ptr);
                                    s->send_serial_data_wait((unsigned char *)ascii_out, strlen(ascii_out));
                                    ascii_out[0] = 0; // don't print last one twice
                                    if (params[i]->fn) params[i]->fn( s, params[i], FN_TYPE_POST_READ, NULL, 0 );
                                    break;
                                default:
                                    break;
                            }
                        }
                    }
                }
            } else {
                int i = 0;
                int count = (sizeof(params)/sizeof(params[0]));
                for (i = 0; i < count; i++){
                    if(params[i] != NULL) {
                        if (params[i]->uistr){
                            if (!strncmp(params[i]->uistr, &cmd[1], strlen(params[i]->uistr))){
                                switch (params[i]->ui_type){
                                    case UI_SHORT:
                                        // if number supplied, write
                                        if ((cmd[1+strlen(params[i]->uistr)] >= '0') && (cmd[1+strlen(params[i]->uistr)] <= '9')){
                                            if (params[i]->fn) params[i]->fn( s, params[i], FN_TYPE_PRE_WRITE, NULL, 0 );
                                            *((short *)params[i]->ptr) = atoi(&cmd[1+strlen(params[i]->uistr)]);
                                            if (params[i]->fn) params[i]->fn( s, params[i], FN_TYPE_POST_WRITE, NULL, 0 );
                                            sprintf(ascii_out, "flash var %s(%s) now %d\r\n",
                                                (params[i]->description)?params[i]->description:"",
                                                params[i]->uistr,
                                                (int)*(short *)params[i]->ptr);
                                        } else {
                                            // read it
                                            if (params[i]->fn) params[i]->fn( s, params[i], FN_TYPE_PRE_READ, NULL, 0 );
                                            sprintf(ascii_out, "%s(%s): %d\r\n",
                                                    (params[i]->description)?params[i]->description:"",
                                                    params[i]->uistr,
                                                    (int)*(short *)params[i]->ptr
                                            );
                                            s->send_serial_data_wait((unsigned char *)ascii_out, strlen(ascii_out));
                                            ascii_out[0] = 0; // don't print last one twice
                                            if (params[i]->fn) params[i]->fn( s, params[i], FN_TYPE_POST_READ, NULL, 0 );
                                        }
                                        break;
                                    default:
                                        sprintf(ascii_out, "flash var %s(%s) unsupported type\r\n",
                                                (params[i]->description)?params[i]->description:"",
                                                params[i]->uistr
                                        );
                                        break;
                                }
                                break; // found our param, now leave
                            }
                        }
                    }
                }
                if (i == count){
                    sprintf(ascii_out, "unknown flash data %s\r\n", cmd);
                }
            }
        }
    }
    return 1;
}


// must be after all params added?
char *get_F_description() {

    char *p = NULL;
    int len = 0;

    // first loop gets len, 2nd loop makes string
    for (int l = 0; l < 2; l++) {
        char *t = "print/set a flash constant (Fa to print all, Fi to default all):\r\n"
                "  Fss - print, Fss<n> - set\r\n";

        len = strlen(t);

        if (p) strcat(p, t);

        for (int i = 0; i < (sizeof(params)/sizeof(params[0])); i++){
            if(params[i] != NULL) {
                if (params[i]->uistr){
                    char tmp[256];
                    snprintf(tmp, sizeof(tmp)-1,
                        "  F%s<n> - %s\r\n",
                            params[i]->uistr,
                            (params[i]->description)?params[i]->description:""
                        );
                    len += strlen(tmp);
                    if (p) strcat(p, tmp);
                }
            }
        }

        if (NULL == p) {
            p = malloc(len+1);
            *p = 0;
        }

    }
    return p;
}


int line_stm32(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
//case 'G':
    immediate_stm32(s, *cmd, ascii_out);
    return 1;
}

int line_hall(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
//case 'H':
    immediate_hall(s, *cmd, ascii_out);
    return 1;
}

int line_immediate(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
//case 'I':
    speedB = 0;
    steerB = 0;
    PWMData.pwm = CLAMP(speedB * SPEED_COEFFICIENT -  steerB * STEER_COEFFICIENT, -1000, 1000);
    SpeedData.wanted_speed_mm_per_sec = speedB;
    dspeeds[0] = dspeeds[1] = speedB;
#ifdef HALL_INTERRUPTS
    PosnData.wanted_posn_mm = HallData[0].HallPosn_mm;
#endif
    if (strlen(cmd) == 1){
        enable_immediate = 1;
        sprintf(ascii_out, "Immediate commands enabled - WASDXHCGQ\r\n>");
    } else {
        switch (cmd[1] | 0x20){
#ifdef FLASH_STORAGE
            case 's':
                enable_immediate = 1;
                control_type = CONTROL_TYPE_SPEED;
                sprintf(ascii_out, "Immediate commands enabled - WASDXHCGQ - Speed control\r\n>");
                break;
            case 'p':
                enable_immediate = 1;
                control_type = CONTROL_TYPE_POSITION;
                sprintf(ascii_out, "Immediate commands enabled - WASDXHCGQ - Position control\r\n>");
                break;
#endif
            case 'w':
                enable_immediate = 1;
                control_type = CONTROL_TYPE_PWM;
                sprintf(ascii_out, "Immediate commands enabled - WASDXHCGQ - Power (pWm) control\r\n>");
                break;
        }
    }
    return 1;
}

int line_sensors(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
//case 'N':
    immediate_sensors(s, *cmd, ascii_out);
    return 1;
}

int line_poweroff_control(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
//case 'P':
    if (strlen(cmd) == 1){
        disablepoweroff = 1;
        powerofftimer = 0;
    } else {
        if ((cmd[1] | 0x20) == 'r'){
            sprintf(ascii_out, "Reset in 500ms\r\n");
            s->send_serial_data_wait((unsigned char *)ascii_out, strlen(ascii_out));
            HAL_Delay(500);
            HAL_NVIC_SystemReset();
        }

        if ((cmd[1] | 0x20) == 'e'){
            disablepoweroff = 0;
            powerofftimer = 0;
        } else {
            int s = -1;
            sscanf(cmd+1, "%d", &s);
            if (s >= 0){
                if (s == 0){
                    powerofftimer = 1; // immediate
                } else {
                    powerofftimer = ((s*1000)/DELAY_IN_MAIN_LOOP);
                }
            }
        }
    }
    sprintf(ascii_out,
        "disablepoweroff now %d\r\n"\
        "powerofftimer now %d\r\n",
        disablepoweroff,
        powerofftimer);
    return 1;
}

int line_test_message(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
//case 'T':
    if (strlen(cmd) < 2){
        sprintf(ascii_out, "Test command needs A N or T qualifier\r\n");
    } else {
        // send a test message in machine protocol
        switch (cmd[1]){
            case 'A':
            case 'a':
                //protocol_send_ack();
                break;
            case 'N':
            case 'n':
                //protocol_send_nack();
                break;
            case 'T':
            case 't':{
                    char tmp[] = { PROTOCOL_SOM_ACK, 0, 5, PROTOCOL_CMD_TEST, 'T', 'e', 's', 't' };
                    protocol_post(s, (PROTOCOL_MSG2*)tmp);
                }
                break;
        }
        // CR before prompt.... after message
        sprintf(ascii_out, "\r\n");
    }
    return 1;
}


int line_reset_firmware(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
//case 'R':
    if (cmd[1] == '!'){
        sprintf(ascii_out, "\r\n!!!!!Resetting!!!!!\r\n");
        s->send_serial_data_wait((unsigned char *)ascii_out, strlen(ascii_out));
        ascii_out[0] = 0;
        HAL_Delay(500);
        HAL_NVIC_SystemReset();
    }
    return 1;
}

int line_read_memory(PROTOCOL_STAT *s, char *cmd, char *ascii_out) {
// memory read hex address
//case 'M':
    unsigned char tmp[100];
    unsigned char *addr = 0;
    unsigned int len = 4;
#ifdef FLASH_STORAGE
    if (cmd[1] == 'f') {
        int res = readFlash( tmp, 100 );
        if (res > 0) {
            addr = tmp;
            len = res;
        }
    } else
#endif
    {
        sscanf(&cmd[1], "%lx,%x", (unsigned long *)&addr, &len);
    }
    strcat( ascii_out, "\r\n" );
    for (int a = 0; a < len; a++) {
        char t[5];
        sprintf(t, "%2.2X ", *(addr+a));
        strcat( ascii_out, t );
        if (!((a+1)&0xf)){
            strcat( ascii_out, "\r\n" );
            s->send_serial_data_wait((unsigned char *)ascii_out, strlen(ascii_out));
            ascii_out[0] = 0;
        }
    }
    strcat( ascii_out, "\r\n" );
    return 1;
}



/////////////////////////////////////////////
// single byte commands at start of command
// - i.e. only after CR of LF and ascii buffer empty
int main_ascii_init(){

    ascii_add_immediate( 'W', immediate_dir, "Faster" );
    ascii_add_immediate( 'S', immediate_dir, "Slower");
    ascii_add_immediate( 'D', immediate_dir, "Lefter");
    ascii_add_immediate( 'A', immediate_dir, "Righter");
    ascii_add_immediate( 'X', immediate_stop, "DisableDrive");
    ascii_add_immediate( 'Q', immediate_quit, "Quit Immediate");
    ascii_add_immediate( 'H', immediate_hall, "display hall data");
    ascii_add_immediate( 'N', immediate_sensors, "display sensor data");
    ascii_add_immediate( 'C', immediate_electrical, "display electrical measurements");
    ascii_add_immediate( 'G', immediate_stm32, "display stm32 specific");
    ascii_add_immediate( 'O', immediate_toggle_control, "toggle control mode");


    ascii_add_line_fn( 'A', line_set_alarm, "set alarm");
    ascii_add_line_fn( 'B', line_toggle_sensor_control, "toggle sensor control");
    ascii_add_line_fn( 'C', line_electrical, "show electrical measurements");
    ascii_add_line_fn( 'S', line_main_timing_stats, "show main loop timing stats");
    ascii_add_line_fn( 'E', line_debug_control, "dEbug control, E->off, Ec->console on, Es->console+scope");

    ascii_add_line_fn( 'R', line_reset_firmware, " - R! -> Reset Firmware");
    ascii_add_line_fn( 'T', line_test_message, "tt - send a test protocol message ");
    ascii_add_line_fn( 'P', line_poweroff_control, " P -power control\r\n"
                "  P -disablepoweroff\r\n"
                "  PE enable poweroff\r\n"
                "  Pn power off in n seconds\r\n"
                "  Pr software reset\r\n" );

    ascii_add_line_fn( 'M', line_read_memory, " M - dump memory\r\n"
                "   Mf - dump flash\r\n"
                "   M<hexaddr>,<hexlen> - dump mem\r\n");
    ascii_add_line_fn( 'N', line_sensors, "display sensor data");
    ascii_add_line_fn( 'I', line_immediate, "enable immediate commands - Ip/Is/Iw - direct to posn/speed/pwm control\r\n");
    ascii_add_line_fn( 'G', line_stm32, "display stm32 specific");

    ascii_add_line_fn( 'F', line_generic_var, get_F_description());

    return 1;
}
/////////////////////////////////////////////

