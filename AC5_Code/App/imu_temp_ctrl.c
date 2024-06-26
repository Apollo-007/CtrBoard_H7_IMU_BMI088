#include "main.h"
#include "cmsis_os.h"
#include "BMI088driver.h"
#include "gpio.h"
#include "tim.h"
#include "kalman_filter.h"
#include "QuaternionEKF.h"
#include "imu_temp_ctrl.h"
#include "MahonyAHRS.h"
#include "pid.h"
#define cheat TRUE  //作弊模式 去掉较小的gyro值	TRUE FALSE
#define correct_Time_define 1000    //上电去0飘 1000次取平均
#define temp_times 100       //探测温度阈值

pid_type_def Temperature_PID={0};
float Temperature_PID_Para[3]={286,0,0};


float gyro[3], accel[3], temp;
float gyro_correct[3]={0};
float RefTemp = 47;   //Destination
float roll,pitch,yaw=0;
uint8_t attitude_flag=0;
uint32_t correct_times=0;
float tempcount = 1;


void INS_Init(void)
{
	
    IMU_QuaternionEKF_Init(10, 0.001, 10000000, 1, 0.001f,0); //ekf初始化
		PID_init(&Temperature_PID, PID_POSITION,Temperature_PID_Para,2000,0); //加热pidlimit
		Mahony_Init(1000);  //mahony姿态解算初始化
    // imu heat init
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
    while(BMI088_init());  //陀螺仪初始化
}

uint32_t temp_temperature=0;
void IMU_Temperature_Ctrl(){
//	if(attitude_flag==0)
//	{
//		Temperature_PID.Kp=120;
//	}
//	else
//	{
//		Temperature_PID.Kp=286;
//	}
	  PID_calc(&Temperature_PID, temp, RefTemp); //温度pid  //需要调一下pid使得温度在47°左右
//		if(temp<RefTemp-5)
//			temp_temperature=600;
//		else if(temp<RefTemp-1.5f)
//			temp_temperature=320;
//		else if(temp<RefTemp-1)
//			temp_temperature=90;
//		else
//		{
			temp_temperature=(uint32_t)Temperature_PID.out; 
			if(Temperature_PID.out<0)
			{
				temp_temperature=0;
			}
//		}
    htim3.Instance->CCR4 = temp_temperature;
}
/***
 * @brief: INS_TASK(void const * argument)
 * @param: argument - 任务参数
 * @retval: void
 * @details: IMU姿态控制任务函数
 
*/
static uint8_t first_mahony=0; 
void INS_Task(void)
{
    static uint32_t count = 1;

    // ins update
    if ((count % 1) == 0)
    {
        BMI088_read(gyro, accel, &temp);
				if(first_mahony==0)
				{
					first_mahony++;
					MahonyAHRSinit(accel[0],accel[1],accel[2],0,0,0);  
				}
				if(attitude_flag==2)  //ekf的姿态解算
				{
					gyro[0]-=gyro_correct[0];   //减去陀螺仪0飘
					gyro[1]-=gyro_correct[1];
					gyro[2]-=gyro_correct[2];
					
					#if cheat              //作弊 可以让yaw很稳定 去掉比较小的值0.003f); 0.02->yaw:0°/h;0.01->yaw:5°/h
						if(fabsf(gyro[2])<0.012f)
							gyro[2]=0;
					#endif
					//===========================================================================
						//ekf姿态解算部分
					//HAL_GPIO_WritePin(GPIOE,GPIO_PIN_13,GPIO_PIN_SET);
					IMU_QuaternionEKF_Update(gyro[0],gyro[1],gyro[2],accel[0],accel[1],accel[2]);
					//HAL_GPIO_WritePin(GPIOE,GPIO_PIN_13,GPIO_PIN_RESET);
					//===============================================================================	
						
					//=================================================================================
					//mahony姿态解算部分
					//HAL_GPIO_WritePin(GPIOE,GPIO_PIN_13,GPIO_PIN_SET);
					Mahony_update(gyro[0],gyro[1],gyro[2],accel[0],accel[1],accel[2],0,0,0);
					Mahony_computeAngles(); //角度计算   移植到别的平台需要替换掉对应的arm_atan2_f32 和 arm_asin
					//HAL_GPIO_WritePin(GPIOE,GPIO_PIN_13,GPIO_PIN_RESET);
					//=============================================================================
					//ekf获取姿态角度函数
					pitch=Get_Pitch(); //获得pitch
					roll=Get_Roll();//获得roll
					yaw=Get_Yaw();//获得yaw
					//==============================================================================
				}
				else if(attitude_flag==1)   //状态1 开始1000次的陀螺仪0飘初始化
				{
						//gyro correct
						gyro_correct[0]+=	gyro[0];
						gyro_correct[1]+=	gyro[1];
						gyro_correct[2]+=	gyro[2];
						correct_times++;
						if(correct_times>=correct_Time_define)
						{
							gyro_correct[0]/=correct_Time_define;
							gyro_correct[1]/=correct_Time_define;
							gyro_correct[2]/=correct_Time_define;
							attitude_flag=2; //go to 2 state
						}
				}
    }

    // temperature control
    if ((count % 2) == 0)
    {
        // 500hz 的温度控制pid
        IMU_Temperature_Ctrl();
				
				static uint32_t temp_Ticks=0;
				if((fabsf(temp-RefTemp)<5)&&attitude_flag==0) //接近额定温度之差小于5° 开始计数
				{
					temp_Ticks++;
					if(temp_Ticks>temp_times)   //计数达到一定次数后 才进入0飘初始化 说明温度已经达到目标
					{
						attitude_flag=1;  //go to correct state
						tempcount=temp;
					}
				}
    }
    count++;
}

void IMU_task(void  * argument)
{
    INS_Init();
    /* Infinite loop */
    for (;;)
    {
        INS_Task();
        osDelay(1);
    }
}


/**
************************************************************************
* @brief:      	HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
* @param:       GPIO_Pin - 触发中断的GPIO引脚
* @retval:     	void
* @details:    	GPIO外部中断回调函数，处理加速度计和陀螺仪中断
************************************************************************
**/
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if(GPIO_Pin == ACC_INT_Pin)
    {
    }
    else if(GPIO_Pin == GYRO_INT_Pin)
    {

    }
}
