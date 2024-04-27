#ifndef SPASH_H
#define SPLASH_H

#include "NuggetInterface.h"

class SplashScreen: public NuggetScreen {
	public:
		SplashScreen(unsigned long durationInMs);
		~SplashScreen(){};
		bool draw();
		int update(int);
	private:
		unsigned long endAt;
};

#endif