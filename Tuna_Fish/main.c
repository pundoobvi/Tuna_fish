#include "Basics.h"
#include "I2C_basics.h"
#include "MPU6050.h"
#include "Math.h"

#define GyroX_offset 13														     //Gyro Offsets Register from 0x13 to 0x18
#define GyroY_offset -25
#define GyroZ_offset -8

#define AccelX_offset 1053														//Accel Offsets Register from 0x06 to 0x0B
#define AccelY_offset -1697
#define AccelZ_offset 1575

#define rad_to_deg 57.2957795131

volatile unsigned int *DWT_CTRL= 		(volatile unsigned int *) 0xE0001000;
volatile unsigned int *DWT_CYCCNT=  (volatile unsigned int *) 0xE0001004;
volatile unsigned int *SCB_DEMCR= 	(volatile unsigned int *) 0xE000EDFC;

GPIO_InitTypeDef GPIO_InitStructure;
I2C_InitTypeDef I2C_InitStructure;

void Enable_PeriphClock(void);
int16_t MPU6050_Offsets[6]={GyroX_offset,GyroY_offset,GyroZ_offset,AccelX_offset,AccelY_offset,AccelZ_offset};

float spudnut,donut;
float delt;

void Display_Raw(float accel[3],float gyro[3],float temp);

bool confirm_offsets(void);
bool cfilter_en=false,kfilter_en=false;

void Attitude_c(float accel[3],float gyro[3], float rpy_c[3]);
void Attitude_k(float accel[3],float gyro[3],float rpy_k[3]);

void RemoveGravity(float ypr_k[3],float accel[3]);

float Roll_est,Roll_predict;								//Estimated states and predicted states
float Pitch_est,Pitch_predict;
float Yaw_est,Yaw_predict;

float P_est[2][2][3],P_predict[2][2][3];		//Process covariance matrix

float Q_angle[3]={0.001,0.001,0},Q_bias[3]={0.003,0.003,0};			//Process Noise covariance matrix

float R_measurement[2]={0.003,0.003};														//Measurement noise covariance matrix

float Gyro_Bias[3];																							//Gyro bias

float Bomb[2];																									//innovation

float K_gain[2][3];																							//Kalman gain

int main()
{
	SerialDebug(250000);
	BeginBasics();
	Blink();
	Enable_PeriphClock();
	
	/*Roll-0 Pitch-1 Yaw-2..Roll rotation around X axis.Pitch rotation around Y axis and Yaw rotation around Z axis
	note: It does not mean rotation along the X Y or Z axis*/
	
	float RPY_c[3],RPY_k[3];																
	float Accel[3],Gyro[3],Tempreature;
	
	while(Init_I2C(400)){PrintString("\nI2C Connection Error");}
	while(MPU6050_Init()){PrintString("MPU6050 Initialization Error");}
	MPU6050_UpdateOffsets(&MPU6050_Offsets[0]);
	//MPU6050_ConfirmOffsets(&MPU6050_Offsets[0]);
	
	
	while(1)
	{
		MPU6050_GetRaw(&Accel[0],&Gyro[0],&Tempreature);
		if(Gyro[2]<0.3 && Gyro[2]>-0.3) Gyro[2]=0;
		spudnut=tics();
		delt=spudnut-donut;
		donut=spudnut;
		
		//Display_Raw(Accel,Gyro,Tempreature);
		
		Attitude_k(Accel,Gyro,RPY_k);
		
		PrintString("\nYaw\tPitch\tRoll\tFreq\t");
		PrintFloat(RPY_k[2]);
		PrintString("\t");
		PrintFloat(RPY_k[1]);
		PrintString("\t");
		PrintFloat(RPY_k[0]);
		PrintString("\t");
		PrintFloat(1/delt);
		
	}
}

void Enable_PeriphClock(void)											//UART and LEDs are Enabled by default by Basics.c..rest we got to enable here.
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);		 	 //Enable GPIO clock
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1,ENABLE);				 //Enable I2C1
}

void Attitude_c(float accel[3],float gyro[3],float rpy_c[3])
{
	if(!cfilter_en)
	{																									//http://www.nxp.com/files/sensors/doc/app_note/AN3461.pdf
		rpy_c[0]= atan2(accel[1],accel[2]) * rad_to_deg;
		rpy_c[1]= atan2(-accel[0],sqrt(accel[1]*accel[1] + accel[2]*accel[2])) * rad_to_deg;
		rpy_c[2]= 0;
		cfilter_en=true;
		PrintString("\nComplementry filter initiated");
	}
	
	float accel_roll=atan2(accel[1],accel[2]) * rad_to_deg;     //calculates from accelerometer readings
  float accel_pitch=atan2(-accel[0],sqrt(accel[1]*accel[1] + accel[2]*accel[2])) * rad_to_deg;
	
	rpy_c[0]= 0.93 * (rpy_c[0] + gyro[0]*delt) + 0.07*accel_roll;
  rpy_c[1]= 0.93 * (rpy_c[1] + gyro[1]*delt) + 0.07*accel_pitch;
	rpy_c[2]= 				rpy_c[2] + gyro[2]*delt;
	/*
	PrintString("\nPitch and Roll\t");
	PrintFloat(ypr_c[2]);
	PrintString("\t");
	PrintFloat(ypr_c[1]);*/
}

void Attitude_k(float accel[3],float gyro[3],float rpy_k[3])
{
	if(!kfilter_en)
	{
		rpy_k[0]= atan2(accel[1],accel[2]) * rad_to_deg;          //http://www.nxp.com/files/sensors/doc/app_note/AN3461.pdf
		rpy_k[1]= atan2(-accel[0],sqrt(accel[1]*accel[1] + accel[2]*accel[2])) * rad_to_deg;
		rpy_k[2]= 0;
		kfilter_en=true;
		PrintString("\nKalman filter initiated");
	}
	
	/*Prediction..of the state variables which are Roll and Gyro X bias and Pitch and Gyro Y Bias*/
																								//Previous_State + (Gyro_reading - Gyro_bias)delT..previous state updated with corrected gyro angular rate
	Roll_predict=  Roll_est  + (gyro[0]- Gyro_Bias[0])  * delt;
	Pitch_predict= Pitch_est + (gyro[1]- Gyro_Bias[1])  * delt;
	Yaw_predict= 	 Yaw_est   + (gyro[2]- Gyro_Bias[2])  * delt;
																								//No prediction for Gyro Bias..it'll be updated by measurement..no direct mesurement possible for gyro bias..it uses measured data to find the drift
	
	/*Prediction of the Process Covariance matrix [P_predict= A* P_prev * A transpose + Q ] where Q is Process Noise Covariance Matrix..
		we assume covariance to be zero so ther are only two elements Q angle and Q bias..variance in angle and variance in bias*/
	
	for(uint8_t i=0; i<2;i++)
	{
		P_predict[0][0][i]= P_est[0][0][i] + delt *( delt * P_est[1][1][i] - P_est[0][1][i] - P_est[1][0][i] + Q_angle[i]);
		P_predict[0][1][i]= P_est[0][1][i] - delt * P_est[1][1][i];
		P_predict[1][0][i]= P_est[1][0][i] - delt * P_est[1][1][i];
		P_predict[1][1][i]= P_est[1][1][i] + Q_bias[i] * delt;
	}
	
	/*Pitch and Roll computed using gravity i.e. via Accelerometers..
	this gets unreliable if there is acceleration in axis prependicular to gravitational axis*/
	
	float rpy_measured[2];
	rpy_measured[0]= atan2(accel[1],accel[2]) * rad_to_deg;
	rpy_measured[1]= atan2(-accel[0],sqrt(accel[1]*accel[1] + accel[2]*accel[2])) * rad_to_deg;
	
	/*Computing Kalman Gain..which we can say is 
	             Error in prediction
	----------------------------------------------
	(Error in prediciton + Error in measurement)
	*/
	
	for(uint8_t i=0;i<2;i++)
	{
		K_gain[0][i]= P_predict[0][0][i]/( P_predict[0][0][i] + R_measurement[i] ) ;
		K_gain[1][i]= P_predict[1][0][i]/( P_predict[1][0][i] + R_measurement[i] ) ;
	}
	
	/*Innovation..Difference b/w Measured value and Predicted value*/
	
	Bomb[0]=rpy_measured[0] - Roll_predict;
	Bomb[1]=rpy_measured[1] - Pitch_predict;
	
	/*Updating predicted states with measured states..
	New_Estimate= Predicted_estimate + Kalman_gain * (Measured state - Predicted state)
	*/
	
	rpy_k[0]=Roll_est= Roll_predict +  K_gain[0][0] * Bomb[0];
	Gyro_Bias[0]= Gyro_Bias[0] + K_gain[1][0] * Bomb[0]; 
	
	rpy_k[1]=Pitch_est= Pitch_predict +  K_gain[0][1] * Bomb[1];
	Gyro_Bias[1]= Gyro_Bias[1] + K_gain[1][1] * Bomb[1];
	
	rpy_k[2]=Yaw_est= Yaw_predict;
	
	/*Updateing Process Covariance matrix Pk= (I- K*H)*Pkp  Pkp is predicted Process covariance matrix*/
	
	for(uint8_t i=0;i<2;i++)
	{
		P_est[0][0][i]= P_predict[0][0][i]*(1-K_gain[0][i]); 
		P_est[0][1][i]= P_predict[0][1][i]*(1-K_gain[0][i]);
		P_est[1][0][i]= P_predict[1][0][i]-(K_gain[1][i] * P_predict[0][0][i]); 
		P_est[1][1][i]= P_predict[1][1][i]-(K_gain[1][i] * P_predict[0][1][i]);
	}
	
	/*
	PrintString("\nGyro Biases\t");
	PrintFloat(Gyro_Bias[0]);
	PrintString("\t");
	PrintFloat(Gyro_Bias[1]);
	PrintString("\t");
	PrintFloat(Gyro_Bias[2]);
	PrintString("\t");
	*/
	
} /*End of fucntion*/

void RemoveGravity(float ypr_l[3],float accel[3])
{
	accel[2]= accel[2];
}

void Display_Raw(float accel[3], float gyro[3], float temp)
{
	PrintString("\nAccel\tTemp\tGyro\t");
	
	for(uint8_t i=0;i<3;i++)
	{
		PrintFloat(accel[i]);
		PrintString("\t");
	}
	
	PrintFloat(temp);
	PrintString("\t");
	
	for(uint8_t i=0;i<3;i++)
	{
		PrintFloat(gyro[i]);
		PrintString("\t");
	}
}

void Enable_DWT()
{
	bool DWT_en=false;
	if(!DWT_en)
	{
		*SCB_DEMCR =  *SCB_DEMCR | 0x01000000;
    *DWT_CYCCNT = 0; 															// reset the counter
    *DWT_CTRL =   *DWT_CTRL | 1 ; 								// enable the counter
	}
}
