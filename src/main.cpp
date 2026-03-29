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


// #define T20
#ifdef T20
	#include "T20_MFCC_008/T20_Main_010.h"
	// #include "T20_MFCC_005/T20_Main_007.h"
#endif


/*
#define T20_11
#ifdef T20_11
    // 1. 버전 정의
    #define VER  083

    // 2. 매크로를 문자열로 변환하기 위한 보조 매크로
    #define TO_STR(x) #x
    #define GET_PATH(v) TO_STR(T20_MFCC_011/v/T20_Main_##v.h)

    // 3. 최종 include (계산된 경로 적용)
    #include GET_PATH(VER)
#endif
*/


#define T20_11
#ifdef T20_11
	#include "T20_MFCC_011/116/T20_Main_116.h"
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



	#ifdef T20_11
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

	#ifdef T20_11
		T20_run();
	#endif

}
