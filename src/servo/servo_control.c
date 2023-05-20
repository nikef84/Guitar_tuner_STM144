#include "servo_control.h"
#include "terminal_write.h"

// Real string freqs. We will tune the guitar to these freqs.
static float trueFreqs[NUM_OF_STRINGS] = {329.63, 246.94, 196, 146.83, 110, 82.4};

// These coefficients are multiplied by the rotation time of each servo. They are selected by experiment.
static float servosCoefHigher[NUM_OF_STRINGS][3] = {{-8.96282 , 5590.04439, -868444.28271 },
													{-1.42526 , 590.92349 , -58888.28755  },
													{-11.84091, 4418.83954, -410839.97327 },
													{-6.52610 , 1777.95508, -119951.73807 },
													{-14.3117 , 2917.8128 , -147636.7151  },
													{-5.49025 , 779.82973 , -26810.52007  }};

static float servosCoefLower[NUM_OF_STRINGS][3] = {{-13.76836, 9415.78265, -1607476.40430},
												   {-9.45670 , 4830.40236, -616054.15732 },
												   {-14.86832, 6041.15841, -612674.91539 },
												   {-11.86021, 3672.94617, -283528.02896 },
												   {-32.33988, 7403.72227, -423102.88013 },
												   {-4.20502 , 793.20842 , -36632.44901  }};

// The rotational speed of each servo. First string - to lower, second - to higher.
// Clocwise(lower): min = 710, max = 510; Counterclockwise: min = 770, max = 1000.
static uint16_t servosSpeed	[2][NUM_OF_STRINGS] = {{820, 870, 920, 960, 950, 1000},
												   {660, 610, 550, 520, 530, 510}};

// The maximum possible differences to light the green color of the string.
static float maxFreqsDiff[NUM_OF_STRINGS] = {5, 5, 3, 3, 3, 1.5};

// Mailboxes for each servo. Because each servo has its own thread.
static mailbox_t mb_servo_1, mb_servo_2, mb_servo_3,
				 mb_servo_4, mb_servo_5, mb_servo_6;

// Mailbox bufferes for each servo.
static msg_t mb_servo_1_buffer[SIZE_OF_MB_BUF],
			 mb_servo_2_buffer[SIZE_OF_MB_BUF],
			 mb_servo_3_buffer[SIZE_OF_MB_BUF],
			 mb_servo_4_buffer[SIZE_OF_MB_BUF],
			 mb_servo_5_buffer[SIZE_OF_MB_BUF],
			 mb_servo_6_buffer[SIZE_OF_MB_BUF];

// Threads of each servo.
static thread_t *tp_servo_1, *tp_servo_2, *tp_servo_3,
				*tp_servo_4, *tp_servo_5, *tp_servo_6;

// Thread wprking areas for each servo.
// 256 - stack size.
static THD_WORKING_AREA(wa_servo_1, 256);
static THD_WORKING_AREA(wa_servo_2, 256);
static THD_WORKING_AREA(wa_servo_3, 256);
static THD_WORKING_AREA(wa_servo_4, 256);
static THD_WORKING_AREA(wa_servo_5, 256);
static THD_WORKING_AREA(wa_servo_6, 256);

/*
 * @brief	Converts a variable of a msg_t type to a variable of a float type.
 *
 * @param[in] 	msg		A variable of a msg_t type, which will be converted to float type.
 *
 * @param[out]			A variable of a float type. The result of a convertion.
 *
 * @notapi
 */
float msg2float(msg_t msg){
	return *((float*) &msg);
}

/*
 * @brief	Converts a variable of a float type to a variable of a msg_t type.
 *
 * @param[in] 	num		A variable of a float type, which will be converted to msg_t type.
 *
 * @param[out]			A variable of a msg_t type. The result of a convertion.
 *
 * @notapi
 */
msg_t float2msg(float num){
	return *((msg_t *) &num);
}

/*
 * @brief	Rotates the servo for a certain period of time.
 *
 * @note	The period of rotation depends on defference
 * 			between real freq of string and current freq of string.
 *
 * @note 	These function is used in every thread.
 *
 * @note	Max period of rotation - ROTATE_TIME_LIM_MAX.
 * 			Min period of rotation - ROTATE_TIME_LIM_MIN.
 *
 * @note 	If the perid of rotation is less than ROTATE_TIME_LIM_MIN,
 * 			than current freq of string is a real one.
 *
 * @param[in]	numOfServo		The number of the servo that we control.
 * 				numOfMailBox	The number of the mailbox, which sends msg to
 * 								the servo that we control.
 *
 * @notapi
 */
void rotate_servo(uint8_t numOfServo, mailbox_t *numOfMailBox){
	msg_t msgResult; // A msg from the mailbox.
	uint16_t timeOfRotation; // A time of rotation of the servo.
	float speedOfRotation;

	// Get a msg from the mailbox.
	msg_t msgError = chMBFetchTimeout(numOfMailBox, &msgResult, TIME_INFINITE);
	float stringFreq = msg2float(msgResult); // Converts a msg from the mailbox to float.


	// If we have a msg and it doesn't equal to 0.
	if (msgError == MSG_OK && stringFreq != 0){
		// Counts the time that will rotate the servo.
		// To higher.
		if (trueFreqs[numOfServo - 1] - stringFreq > 0){
			speedOfRotation = servosSpeed[SET_FREQ_HIGHER][numOfServo - 1];
			timeOfRotation = roundf((servosCoefHigher[numOfServo - 1][0] * stringFreq * stringFreq) +
									(servosCoefHigher[numOfServo - 1][1] * stringFreq) +
									(servosCoefHigher[numOfServo - 1][2]));
		}
		// To lower.
		else {

			speedOfRotation = servosSpeed[SET_FREQ_LOWER][numOfServo - 1];
			timeOfRotation = roundf((servosCoefLower[numOfServo - 1][0] * stringFreq * stringFreq) +
									(servosCoefLower[numOfServo - 1][1] * stringFreq) +
									(servosCoefLower[numOfServo - 1][2]));
//			dbgPrintf("1 = %0.3f\r\n", servosCoefHigher[numOfServo - 1][0] * stringFreq * stringFreq);
//			dbgPrintf("2 = %0.3f\r\n", servosCoefHigher[numOfServo - 1][1] * stringFreq);
//			dbgPrintf("3 = %0.3f\r\n", servosCoefHigher[numOfServo - 1][2]);
		}
		dbgPrintf("time = %d\r\n", timeOfRotation);
		dbgPrintf("speed = %0.3f\r\n", speedOfRotation);

		// If the calculated time is greater than the maximum rotation time.
		if (timeOfRotation >= ROTATE_TIME_LIM_MAX) timeOfRotation = ROTATE_TIME_LIM_MAX;

		// If we need to rotate the servo to tune the string.
		if (fabs(trueFreqs[numOfServo - 1] - stringFreq) > 0.3){
			servoSetVoltage(numOfServo, speedOfRotation);

			// Rotate the servo for a certain period of time.
			chThdSleepMilliseconds(timeOfRotation);

			// Stops the servo.
			servoStop(numOfServo);
			// Sends msg to indication thread.
			if (fabs(trueFreqs[numOfServo - 1] - stringFreq) < maxFreqsDiff[numOfServo - 1]){
				setStringLeds(LED_GREEN_LIGHT, numOfServo);
			}
			else setStringLeds(LED_RED_LIGHT, numOfServo);
		}
		else setStringLeds(LED_GREEN_LIGHT, numOfServo);

	}
}

/*
 * @brief	The thread that controls the 1st servo.
 *
 * @note 	The thread falls asleep until a message arrives in its mailbox.
 */
static THD_FUNCTION(servo_1, arg){
	chRegSetThreadName("Servo 1");

	(void)arg;
	while (!chThdShouldTerminateX()){
		rotate_servo(STRING_1, &mb_servo_1);
	}
	chThdExit(MSG_OK);
}

/*
 * @brief	The thread that controls the 2nd servo.
 *
 * @note 	The thread falls asleep until a message arrives in its mailbox.
 */
static THD_FUNCTION(servo_2, arg){
	chRegSetThreadName("Servo 2");

	(void)arg;
	while (!chThdShouldTerminateX()){
		rotate_servo(STRING_2, &mb_servo_2);
	}
	chThdExit(MSG_OK);
}

/*
 * @brief	The thread that controls the 3rd servo.
 *
 * @note 	The thread falls asleep until a message arrives in its mailbox.
 */
static THD_FUNCTION(servo_3, arg){
	chRegSetThreadName("Servo 3");

	(void)arg;
	while (!chThdShouldTerminateX()){
		rotate_servo(STRING_3, &mb_servo_3);
	}
	chThdExit(MSG_OK);
}

/*
 * @brief	The thread that controls the 4th servo.
 *
 * @note 	The thread falls asleep until a message arrives in its mailbox.
 */
static THD_FUNCTION(servo_4, arg){
	chRegSetThreadName("Servo 4");

	(void)arg;
	while (!chThdShouldTerminateX()){
		rotate_servo(STRING_4, &mb_servo_4);
	}
	chThdExit(MSG_OK);
}

/*
 * @brief	The thread that controls the 5th servo.
 *
 * @note 	The thread falls asleep until a message arrives in its mailbox.
 */
static THD_FUNCTION(servo_5, arg){
	chRegSetThreadName("Servo 5");

	(void)arg;
	while (!chThdShouldTerminateX()){
		rotate_servo(STRING_5, &mb_servo_5);
	}
	chThdExit(MSG_OK);
}

/*
 * @brief	The thread that controls the 6th servo.
 *
 * @note 	The thread falls asleep until a message arrives in its mailbox.
 */
static THD_FUNCTION(servo_6, arg){
	chRegSetThreadName("Servo 6");

	(void)arg;
	while (!chThdShouldTerminateX()){
		rotate_servo(STRING_6, &mb_servo_6);
	}
	chThdExit(MSG_OK);
}

/*
 * @brief	Inits servo threads, mailboxes, pwms and sets legs.
 *
 * @note    PWMD3 is used (TIM1).
 *          PWMD4 is used (TIM4).
 *
 * @param[out]		All threads, mailboxes and pwms have been started.
 */
msg_t servoInit(void){
	// Inits servo mailboxes.
	chMBObjectInit(&mb_servo_1, mb_servo_1_buffer, SIZE_OF_MB_BUF);
	chMBObjectInit(&mb_servo_2, mb_servo_2_buffer, SIZE_OF_MB_BUF);
	chMBObjectInit(&mb_servo_3, mb_servo_3_buffer, SIZE_OF_MB_BUF);
	chMBObjectInit(&mb_servo_4, mb_servo_4_buffer, SIZE_OF_MB_BUF);
	chMBObjectInit(&mb_servo_5, mb_servo_5_buffer, SIZE_OF_MB_BUF);
	chMBObjectInit(&mb_servo_6, mb_servo_6_buffer, SIZE_OF_MB_BUF);

	// Inits servo threads.
	tp_servo_1 = chThdCreateStatic(wa_servo_1, sizeof(wa_servo_1), NORMALPRIO, servo_1, NULL);
	tp_servo_2 = chThdCreateStatic(wa_servo_2, sizeof(wa_servo_2), NORMALPRIO, servo_2, NULL);
	tp_servo_3 = chThdCreateStatic(wa_servo_3, sizeof(wa_servo_3), NORMALPRIO, servo_3, NULL);
	tp_servo_4 = chThdCreateStatic(wa_servo_4, sizeof(wa_servo_4), NORMALPRIO, servo_4, NULL);
	tp_servo_5 = chThdCreateStatic(wa_servo_5, sizeof(wa_servo_5), NORMALPRIO, servo_5, NULL);
	tp_servo_6 = chThdCreateStatic(wa_servo_6, sizeof(wa_servo_6), NORMALPRIO, servo_6, NULL);

	// Inits pwms and sets legs.
	servoSimpleInit();

	// Stops all servos. Just in case.
	servoAllStop();

	return MSG_OK;
}

/*
 * @brief	Distributes tasks to each sevro.
 *
 * @note	A msg contains a current freq of the string.
 *
 * @param[in]   stringParams    The pointer to the structure in which all data of strings are stored.
 */
void servoControl(stringFreqsParams *stringParams){
	msg_t stringFreq; // The frequency of the current string.

	// Sends a task to the 1st servo.
	stringFreq = float2msg(stringParams->result[STRING_1 - 1]);
	chMBPostTimeout(&mb_servo_1, stringFreq, TIME_IMMEDIATE);

	// Sends a task to the 2st servo.
	stringFreq = float2msg(stringParams->result[STRING_2 - 1]);
	chMBPostTimeout(&mb_servo_2, stringFreq, TIME_IMMEDIATE);

	// Sends a task to the 3st servo.
	stringFreq = float2msg(stringParams->result[STRING_3 - 1]);
	chMBPostTimeout(&mb_servo_3, stringFreq, TIME_IMMEDIATE);

	// Sends a task to the 4st servo.
	stringFreq = float2msg(stringParams->result[STRING_4 - 1]);
	chMBPostTimeout(&mb_servo_4, stringFreq, TIME_IMMEDIATE);

	// Sends a task to the 5st servo.
	stringFreq = float2msg(stringParams->result[STRING_5 - 1]);
	chMBPostTimeout(&mb_servo_5, stringFreq, TIME_IMMEDIATE);

	// Sends a task to the 6st servo.
	stringFreq = float2msg(stringParams->result[STRING_6 - 1]);
	chMBPostTimeout(&mb_servo_6, stringFreq, TIME_IMMEDIATE);

}

/*
 * @brief	Stops servo threads and pwms.
 *
 * @param[out]    msg   A message is about if thread is stopped or not.
 *
 * @note   Not verified.
 */
msg_t servoUninit(void){
	// Sends a msg to stop threads.
	chThdTerminate(tp_servo_1);
	chThdTerminate(tp_servo_2);
	chThdTerminate(tp_servo_3);
	chThdTerminate(tp_servo_4);
	chThdTerminate(tp_servo_5);
	chThdTerminate(tp_servo_6);

	// Waits for some time until the threads stop.
	msg_t msg[NUM_OF_STRINGS];
	msg[STRING_1 - 1] = chThdWait(tp_servo_1);
	msg[STRING_2 - 1] = chThdWait(tp_servo_2);
	msg[STRING_3 - 1] = chThdWait(tp_servo_3);
	msg[STRING_4 - 1] = chThdWait(tp_servo_4);
	msg[STRING_5 - 1] = chThdWait(tp_servo_5);
	msg[STRING_6 - 1] = chThdWait(tp_servo_6);

	servoSimpleUninit(); // Stops pwms.

	// Check if all threads were stopped.
	for (uint8_t i = 0; i < NUM_OF_STRINGS; i++){
		if (msg[i] != MSG_OK) return msg[i];
	}

	return MSG_OK;
}
