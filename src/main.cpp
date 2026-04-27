#include <Arduino.h>


#define T20_11
#ifdef T20_11
    #include "T20_MFCC_230/231/T200_Main_231.h"
    // #include "T20_MFCC_212/216/T200_Main_216.h"
	// #include "T20_MFCC_212/214/T200_Main_214.h"
	// #include "T20_MFCC_011/212/T20_Main_211.h"
#endif

//// #define T4_MC
#ifdef T4_MC
	#include "T4_MC_001/003/T400_Main_003.hpp"
#endif


void setup() {
	// delay(5000);

	Serial.begin(115200);
	Serial.print("setup: ");


	#ifdef T20_11
		T20_init();
	#endif
	
	#ifdef T4_MC
		T4_init();
	#endif


}

void loop() {

	#ifdef T20_11
		T20_run();
	#endif
	
	#ifdef T4_MC
		T4_run();
	#endif
}
