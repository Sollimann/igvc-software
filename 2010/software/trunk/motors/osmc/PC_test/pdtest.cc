
#include <iostream>
#include <ctime>
#include "OSMC_driver.hpp"
//#include <>

//static const int speedsetdel = 3 * 1e4;
static const int speedsetdel = 2 * 1e4;

int main()
{
	OSMC_driver drive;

	drive.setVel_pd(.5,.5);

	timeval t;
	gettimeofday(&t, NULL);
	double t0 = double(t.tv_sec) + double(1e-6)*double(t.tv_usec);

	for(;;)
	{
		double left, right;
		byte lpwm, rpwm;

		drive.updateVel_pd();
		gettimeofday(&t, NULL);
		double t1 = double(t.tv_sec) + double(1e-6)*double(t.tv_usec);

		drive.getEncoderVel(right, left);
		drive.getLastPWMSent(rpwm, lpwm);
		std::cout << t1 - t0 << "\tleft v: " << left << "right v: " << right << std::endl;
		std::cout << t1 - t0 << "\tleft pwm: " << lpwm << "right pwm: " << rpwm << std::endl;

		usleep(speedsetdel);
	}
	drive.setmotorPWM(MC_MOTOR_FORWARD, 0, MC_MOTOR_FORWARD, 0);


}
