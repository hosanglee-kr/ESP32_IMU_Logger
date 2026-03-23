#include <Arduino.h>



// #define T002
#ifdef T002
	#include "T002/T002_BMI270_Main_EampAll_005.h"
#endif

// #define T03
#ifdef T03
	#include "T003/T03_Main_007.h"
#endif


//// #define T04
#ifdef T04
	#include "T004/A10_Main_014.h"
#endif


#define T20
#ifdef T20
	#include "T20_MFCC_008/T20_Main_008.h"
	// #include "T20_MFCC_005/T20_Main_007.h"
#endif


void setup() {
	// delay(5000);

	Serial.begin(115200);
	Serial.print("setup: ");

	#ifdef T002
		B10_init();
	#endif

	#ifdef T03
		B10_init();
	#endif

	#ifdef T04
		A10_init();
	#endif
	
	#ifdef T20
		T20_init();
	#endif


}

void loop() {
	#ifdef T002
		B10_run();
		delay(5);
	#endif

	#ifdef T03
		B10_run();
	#endif

	#ifdef T04
		A10_run();
	#endif
	
	
	#ifdef T20
		T20_run();
	#endif



}
