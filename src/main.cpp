#include <Arduino.h>



#define T002
#ifdef T002
	#include "T002/T002_BMI270_Main_EampAll_005.h"
#endif

void setup() {
	// delay(5000);

	Serial.begin(115200);
	Serial.print("setup: ");

	#ifdef T002
		B10_init();
	#endif

}

void loop() {
	#ifdef T002
		B10_run();
		delay(5);
	#endif

}
