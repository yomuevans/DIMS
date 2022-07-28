/*
Damon's Reference XCAM Implementation
2021-08-24
*/
#include "D_XCAM.h"
#include "TaskMonitor.h"
#include "EPS.h"

#define D_XCAM_ADDRESS (0x66)
#define D_XCAM_DEBUG (1)
#define D_XCAM_HALOK (0)
#define D_XCAM_RAWSTATUS (1)

#define XCAM_BUSY (0x01)


//XCAM_Registers
#define XCAM_SENSOR        (0)   // use 1, WFI at HAO
#define XCAM_INTTIME       (1)
#define XCAM_GRAB          (2)
#define XCAM_AUTOEXP       (3)
#define XCAM_HEALTH        (4)
#define XCAM_COMPRESS      (7)
#define XCAM_THUMB         (8)
#define XCAM_WINDOW       (11)   // 0x0B
#define XCAM_TIMEOUT      (18)   // 0x12
#define XCAM_INTTIMEFRAC  (19)   // 0x13
#define XCAM_OP_ERROR     (21)   // 0x15


uint16_t XCAM_sensor   =  1; // 1 == WFI
uint16_t XCAM_autoexp  =  0; // 1 == use auto exposure
uint16_t XCAM_timeout  = 30; // the time C3D goes back to standby
                             // after a grab command
uint16_t XCAM_inttime  =  200;
// defined in Middlewares/ESTTC.h
// extern volatile uint16_t XCAM_inttime_desired;
uint16_t XCAM_inttimefrac = 0;  // just an adhoc value for now
uint16_t XCAM_window   =  0;
uint16_t XCAM_compress =  0; // = 0 no compression, 1=compression
uint16_t XCAM_thumb    =  0; // = 0 no thumbnail  , 1=thumbnail
uint16_t XCAM_grab     =  1; // = 0 no grab, 1=grab an image
uint8_t  XCAM_mode     =  1; // 1=imaging mode

#define DEBUG (1)
/* Global Vars */
RTC_TimeTypeDef currentTime;
RTC_DateTypeDef currentDate;
time_t timestamp;
struct tm currTime;
#include <time.h>


/*******************************************************************************
 * @brief  Begin Exposure by setting the Grab command to 1.
 * @param  None
 * @retval (0) if success, (1) if error
 * @note   This function is called by the main program.
 * ****************************************************************************/
D_XCAM_BeginExposure(){
  if(D_XCAM_SetParameter(XCAM_GRAB, 1)){
    print("D_XCAM_SetParameter(XCAM_GRAB, 1) failed\n");
    return 1;
  }
  return 0;
}

/**
  * @brief  Launches recovery based on HAL Issues
  * @param  ret : The HAL_StatusTypeDef notrmal return type of the HAL System
  * @retval none
  */
void HAL_Recovery_Tree(HAL_StatusTypeDef ret){
   //Findout what error we are getting Error
   if(ret == HAL_ERROR){
    #ifdef DEBUG
       fprintf(PAYLOAD,"HAL has an error\n\r");
    #endif
    HAL_Recovery_State_Busy();

   }else if(ret == HAL_BUSY){
    #ifdef DEBUG
       fprintf(PAYLOAD,"HAL Was Busy\n");
    #endif
    HAL_Recovery_State_Busy();

   }else if(ret == HAL_TIMEOUT){
    #ifdef DEBUG
       fprintf(PAYLOAD,"HAL Timed Out\n");
    #endif
       HAL_Recovery_State_Busy();
       //Run Timeout Recovery

   }else if (ret == HAL_OK){
    #ifdef DEBUG
      fprintf(PAYLOAD,"And you may ask yourself, \"Well, how did I get here?\"\nHAL Was Okay\n");
    #endif
   }
   return;
}



//Recovery From a busy IO port
//This should only be used if we have been busy for way longer than expected
//Recovery Tree-->Recovery Busy State -> Recovery Tree
void HAL_Recovery_State_Busy(void){
    HAL_StatusTypeDef ret;
    HAL_I2C_DeInit(&hi2c3);         //Release IO port as GPIO, reset handle status flag
    ret = HAL_I2C_Init(&hi2c3);
    HAL_Recovery_Tree(ret);


}


/**
*  @brief 
*  @param XCAM_Status: This is the Current Status of the XCAM
*  @retval none
*/
void XCAM_Recovery_Tree(uint8_t XCAM_Status){
  //Invalid Opp
  if (XCAM_Status == 0x02){

  }

}


/**
  * @brief  This should Adjust the Exposure Based on the given Setting
  * @param  setting : Duration of Exposure in units of 157us Note: if set to '0' the camera will use Auto Exposure
  * @retval none
  */
void Adjust_Exposure(uint16_t setting){
    if(setting == 0){
        // set auto-exposure mode
        D_XCAM_SetParameter(XCAM_AUTOEXP, 1);
        //TO_DO: Do I need to change the Set Exposure time

    }else{
        //Set the Exposure Time
        D_XCAM_SetParameter(XCAM_INTTIME, setting);
        D_XCAM_SetParameter(XCAM_AUTOEXP, 0);

    }
    return;
}



/**
  * @brief  The main Loop of taking exposures with the Camera will cycle through given exposures
  * @param  none
  * @retval none
  */
void Main_Camera_Loop(void){
    uint8_t D_XCAM_Status[22] = {0};
    uint8_t PayloadSPI[260] = {0};
    uint8_t PayloadI2C[260] = {0};
    uint16_t packetsRemaining;

    // set the SPI nCS pin high (disable)
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

      D_XCAM_WaitSeconds(2, true);  // wait for the other tasks to stop printing text
      D_XCAM_Power_Cycle();

      while(D_XCAM_Init()){
        #ifdef DEBUG
          fprintf(PAYLOAD, "We got an issue Initializing\n\rAttempting Again\r\n");
        #endif
          TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);

      }

     // Write_To_File();
    //  4) Write to parameter 0x00 to identify which interface you wish to acquire an image from (0 for SI0, 1 for SI1, 2 for SI2).
      D_XCAM_SetParameter(0x00, 1);
      TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);

    //  5) Write to parameter 0x10 the file address where you wish the images to be stored. This will be included in each packet as an unsigned short in bytes 6 and 7.
      D_XCAM_SetParameter(0x10, 0);

      uint8_t len = 4;
      uint16_t Exposures[4];
      Exposures[0] = 4000;
      Exposures[1] = 3000;
      Exposures[2] = 2000;
      Exposures[3] = 0;

      uint8_t i = 0;
      while(1){
          //If the Index is too high we need to restart fom the beginning
          if(i > len){
              i = 0;
          }
        // set exposure time
          Adjust_Exposure(Exposures[i]);

        //  6) Update the payload operation mode to 0x01 for imaging operations.
          uint8_t ret = D_XCAM_EnableImagingMode();
          if(ret == 1){
              #ifdef DEBUG
              fprintf(PAYLOAD,"Could not transmit Enable Imaging Mode");
              #endif
          }if(ret == 2){
              #ifdef DEBUG
              fprintf(PAYLOAD,"Could not recieve Enable Imaging Mode");
              #endif
          }
        //  7) A bitwise payload status flag of 0x01 indicates the payload is currently busy in the operation cycle.
          D_XCAM_GetStatus(D_XCAM_Status);
          D_XCAM_AnalyzeStatus(D_XCAM_Status, &packetsRemaining);
          TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);

        //  8) Write to parameter 0x02 a value of 0x01 to initiate image capture. Image capture takes approximately 1s. If the grab command is not received within the timeout, C3D will register a mode failure.
          D_XCAM_SetParameter(0x02, 0x01); // begin capture
          TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);
          uint8_t counter = 0;

          while((counter<=90)&&(!(D_XCAM_AnalyzeStatus(D_XCAM_Status,&packetsRemaining) & 0x02))){
              fprintf(PAYLOAD, "Waiting for image...");
              D_XCAM_WaitSeconds(1, true);
          //  9) A payload status flag of 0x02 indicates the image capture is complete and the data packets have been successfully compiled. The payload will then return to standby mode 0x00.
          // 10) If the payload status flag reads bitwise 0x10 then the operation has failed for some reason (refer to section 6.5.3 for details). The payload will attempt to complete each operation three times before returning this code.
            D_XCAM_GetStatus(D_XCAM_Status);
            D_XCAM_AnalyzeStatus(D_XCAM_Status, &packetsRemaining);

            //Send an Alive Signal
            if(counter%20==0){
                #ifdef DEBUG
                fprintf(PAYLOAD, "Sending an Alive Signal\r\n");
                #endif
                TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);
            }

            counter = counter+1;

          }

          if(counter>30){
              #ifdef DEBUG
              fprintf(PAYLOAD, "Took too Long to Get an Image Resetting");
              #endif
              Main_Camera_Loop();
          }


          //while (!(D_XCAM_AnalyzeStatus(D_XCAM_Status) & 0x02));
          #ifdef DEBUG
          fprintf(PAYLOAD, "Image captured!\r\n");
          #endif
        // 11) The payload data packets waiting will be incremented as the payload returns to standby, to reflect the image packets waiting in the payload memory.
        // 12) Provided the default parameters are still loaded, the data packets waiting will contain an uncompressed thumbnail image and a compressed, unwindowed full image.
        // 13) Data can now be downloaded by the platform. Both I2C download commands will be treated identically by the payload.
         // D_XCAM_GetEntireImageI2C(PayloadI2C);
         // Write_Image_To_SD(PayloadI2C, 260);
          TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);
          //D_XCAM_GetImageSPI(PayloadSPI);
          TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);

        // 14) If the payload status flag reads bitwise 0x08 during a data transfer then no more packets are waiting in memory
          #ifdef DEBUG
          fprintf(PAYLOAD,"Damon's XCAM example is done. Halting.\n\r");
          #endif

          TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);

          //increment index for exposures
          i=i+1;
      }//End While Forever loop


}


void D_XCAM_Example(void){

  uint8_t D_XCAM_Status[22] = {0};
  uint8_t PayloadSPI[260] = {0};
  uint8_t PayloadI2C[260] = {0};
  uint16_t packetsRemaining;
  // set the SPI nCS pin high (disable)
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

  D_XCAM_WaitSeconds(2, true);  // wait for the other tasks to stop printing text
  D_XCAM_Power_On();
  
  D_XCAM_Init();

 // Write_To_File();
//  4) Write to parameter 0x00 to identify which interface you wish to acquire an image from (0 for SI0, 1 for SI1, 2 for SI2).
  D_XCAM_SetParameter(0x00, 1);
  TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);

//  5) Write to parameter 0x10 the file address where you wish the images to be stored. This will be included in each packet as an unsigned short in bytes 6 and 7.
  D_XCAM_SetParameter(0x10, 0);
  
// set exposure time
//  D_XCAM_SetParameter(0x01, 4000);

// set auto-exposure mode
  D_XCAM_SetParameter(0x03, 0x01);

//  6) Update the payload operation mode to 0x01 for imaging operations.
  D_XCAM_EnableImagingMode();

//  7) A bitwise payload status flag of 0x01 indicates the payload is currently busy in the operation cycle.
  D_XCAM_GetStatus(D_XCAM_Status);
  D_XCAM_AnalyzeStatus(D_XCAM_Status, &packetsRemaining);
  TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);

//  8) Write to parameter 0x02 a value of 0x01 to initiate image capture. Image capture takes approximately 1s. If the grab command is not received within the timeout, C3D will register a mode failure.
  D_XCAM_SetParameter(0x02, 0x01); // begin capture
  TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);

  do{
    fprintf(PAYLOAD, "Waiting for image...\n\r");
    D_XCAM_WaitSeconds(1, true);
  //  9) A payload status flag of 0x02 indicates the image capture is complete and the data packets have been successfully compiled. The payload will then return to standby mode 0x00.
  // 10) If the payload status flag reads bitwise 0x10 then the operation has failed for some reason (refer to section 6.5.3 for details). The payload will attempt to complete each operation three times before returning this code.
    D_XCAM_GetStatus(D_XCAM_Status);
  }

  while (!(D_XCAM_AnalyzeStatus(D_XCAM_Status, D_XCAM_AnalyzeStatus) & 0x02));
  fprintf(PAYLOAD, "Image captured!\r\n");
// 11) The payload data packets waiting will be incremented as the payload returns to standby, to reflect the image packets waiting in the payload memory.
// 12) Provided the default parameters are still loaded, the data packets waiting will contain an uncompressed thumbnail image and a compressed, unwindowed full image.
// 13) Data can now be downloaded by the platform. Both I2C download commands will be treated identically by the payload.
  D_XCAM_GetImageI2C(PayloadI2C);
  Write_Image_To_SD(PayloadI2C, 260);
  TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);
  D_XCAM_GetImageSPI(PayloadSPI);
  TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);
  
// 14) If the payload status flag reads bitwise 0x08 during a data transfer then no more packets are waiting in memory

  fprintf(PAYLOAD,"Damon's XCAM example is done. Halting.\n\r");
  
  while(1)
  {
    osDelay(100);
    TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);
  }
}




/**
 * @brief: This function Turns off the XCAM Device by first turning off the 5V then waiting 5 sec and Turning off the 3v3 bus
 * @param: none
 * @retvalue: none
 */ 
void D_XCAM_Power_Off(void){
  #ifdef DEBUG
  fprintf(PAYLOAD,"\r\tTurning off LUP\n\r");
  #endif
  EPS_write(6,1);  //  --> turn off LUP 5v
  D_XCAM_WaitSeconds(5, true);
  EPS_write(5,1);  //  --> turn off LUP 3.3v
  // wait for everything to settle
  D_XCAM_WaitSeconds(5, true);
  EPS_check(1,1);
  return;
}

/**
 * @brief: This function Turns on the XCAM Device by first turning on the 3v3 then waiting 5 sec and Turning on the 5 bus
 * @param: none
 * @retvalue: none
 */ 
void D_XCAM_Power_On(void){
  #ifdef DEBUG
  fprintf(PAYLOAD,"\r\tTurning on LUP\n\r");
  #endif
  EPS_write(5,0);  //  --> turn on LUP 3.3v
  // wait for everything to settle
  D_XCAM_WaitSeconds(5, true);
  EPS_write(6,0);  //  --> turn on LUP 5v
  EPS_check(1,1);
  return;
}

/**
* @brief: This will turn the Power off on the XCAM than turn it back on
* @param: none
* @retvalue: none
*/ 
void D_XCAM_Power_Cycle(void){
  TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);
  D_XCAM_Power_Off();
  D_XCAM_WaitSeconds(5, true);
  TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);
  D_XCAM_Power_On();
  D_XCAM_WaitSeconds(30, 1);
  TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);
}


/** 
*  @brief: This function Initializes the XCAM by Sending the Payload initialize command. Leave it alone for 5 sec
*  @param: none
*  @retvalue: (1) if the first Init did not run sucessfully
*             (2) if the second Init did not run sucessfully
*             (0) if the Init ran sucessfully
*/
uint8_t D_XCAM_Init(void){
//  The start-up sequence for C3D is as follows:
//1) Power on the 3V3 line to initialise the I2C buffer
//2) Power on the 5V line to boot the FPGA
//3) Send the payload a ‘payload initialise command’ to complete the start-up sequence. This will boot the remaining peripheral devices.
  if (D_XCAM_SendInitCommand())
    return 1;
//4) Leave for five seconds to allow the secondary CPU to boot
  D_XCAM_WaitSeconds(5, true);
// Need to send a new init command.
  if (D_XCAM_SendInitCommand())
    return 2;
  return 0;
}


/**
 * @brief : This function Will Analyze the Status of the XCAM and return the number of packets waiting in the payload memory
*  @param *status: a pointer to where the status lives
*  @param prioriryData: pointer to the register to store the number of priority data packets
*  @retvalue: opperationFlag Hex info about the current opperation Flags
*/
uint16_t D_XCAM_AnalyzeStatus(uint8_t *status, uint16_t *priorityData)
{
  uint8_t OperationMode = status[2];
  uint16_t OperationFlag = (status[3] << 8) | status[4];
  uint16_t PriorityData = (status[5] << 8) | status[6];
  uint32_t TotalData = (status[7] << 24) | (status[8] << 16) | (status[9] << 8) | status[10];

  if (OperationMode){
    fprintf(PAYLOAD, "\tImaging Mode, Flags:");
  }else{
    fprintf(PAYLOAD, "\tStandby Mode, Flags:");
  }
  if (OperationFlag & 0x01){
    fprintf(PAYLOAD, " BUSY\n\r");
  }
  if (OperationFlag & 0x02){
    fprintf(PAYLOAD, " FINISHED\n\r");
  }
  if (OperationFlag & 0x08){
    fprintf(PAYLOAD, " NOPACKETS\n\r");
  }
  if (OperationFlag & 0x10){
    fprintf(PAYLOAD, " OPERR\n\r");
    uint8_t status[22] = {0};
    D_XCAM_ReadErrorParameter(status);
    D_XCAM_AnalyzeError(status);
  }
  if (OperationFlag & 0x20){
    fprintf(PAYLOAD, " INVALID\n\r");
  }
  *priorityData = PriorityData;
  fprintf(PAYLOAD, "\r\t%d priority, %ld total packets.\n\r", PriorityData, TotalData);
  return OperationFlag;
}


/**
* @brief: This Finction Will get the operation Status of the XCAM and Return it to the status address
* @param status: pointer to where to save the Data
* @retval: returns based on Sucess (0) or failure (1) did not transmit (2) did not recieve 
*/
uint8_t D_XCAM_GetStatus(uint8_t *status)
{
  uint8_t txbuf[4] = {0};
  uint8_t i;

  for (i=0;i<22;i++)
    status[i] = 0;
 
  txbuf[0] = 0x91;
  txbuf[1] = 1;

  D_XCAM_SetCRC(txbuf, 4);
  if (D_XCAM_DEBUG)
    fprintf(PAYLOAD, "Requesting camera status.\n\r");
  if (D_XCAM_transmit(txbuf, 4))
    return 1;
  osDelay(3);
  if (D_XCAM_receive(status, 22, true))
    return 2;

  if (D_XCAM_RAWSTATUS)
  {
    fprintf(PAYLOAD, "Status: 0x");
    for (i=0; i<22; i++)
      fprintf(PAYLOAD, "%02x ", status[i]);
    fprintf(PAYLOAD, "\r");
  }
  return 0;
}    



/**
 * @brief: This will read The Error Register
 * @param: none
 * @retval: returns 0 if it worked well 1 if did not transmit 2 if did not revieve
 */
uint8_t D_XCAM_ReadErrorParameter(uint8_t *status){
    uint8_t i;
    uint8_t txbuf[5] = {0};
    

    txbuf[0] = 0x94;
    txbuf[1] = 1;
    txbuf[2] = 0x15;
    D_XCAM_SetCRC(txbuf, 5);

    for (i=0;i<22;i++)
     status[i] = 0;
    if (D_XCAM_DEBUG)
     fprintf(PAYLOAD, "Requesting camera error.\n\r");
    if (D_XCAM_transmit(txbuf, 5)){
        fprintf(PAYLOAD,"Could not transmit\r\n");
        return 1;
    }
    osDelay(3);
    if (D_XCAM_receive(status, 22, true)){
     fprintf(PAYLOAD,"Could not receive\r\n");
     return 2;
    }
    if (D_XCAM_RAWSTATUS)
    {
     fprintf(PAYLOAD, "Status: 0x");
     for (i=0; i<22; i++)
       fprintf(PAYLOAD, "%02x ", status[i]);
     fprintf(PAYLOAD, "\r\n");
    }
    return 0;


}



/**
* @brief:Analyze the error status of the XCAM *
* @param: status: pointer to where the error status lives
* @retval: OperationFlag: The Curent Opperation Flag
*/
uint16_t D_XCAM_AnalyzeError(uint8_t *status)
{
  uint16_t OperationFlag = (status[2] << 8) | status[3];
  fprintf(PAYLOAD, "Parsing Status\n\r");
  if (OperationFlag & 0x01){
    fprintf(PAYLOAD, " Insufficient Memory\n\r");
  }
  if (OperationFlag & 0x02){
    fprintf(PAYLOAD, " Auto Exposure Failure\n\r");
  }
  if (OperationFlag & 0x03){
    fprintf(PAYLOAD, " Grab Timeout\n\r");
  }
  if (OperationFlag & 0x05){
    fprintf(PAYLOAD, " CIS Setup Failure\n\r");
  }
  if (OperationFlag & 0x06){
    fprintf(PAYLOAD, " CIS Grab Failure\n\r");
  }
    if (OperationFlag & 0x07){
    fprintf(PAYLOAD, " Invalid Parameter Combination\n\r");
  }
  
  return OperationFlag;
}

/**
 * @brief : This will pull a single image packet from the XCAM
 * @param buffer : Pointer to the buffer
 * @return (1) if it did not transmit (2) issue with HAL (3) if it did not receive (0)everything Went Well
 */ 
uint8_t D_XCAM_GetImageSPI(uint8_t *buffer)
{
  HAL_StatusTypeDef SPI_ret;
  uint8_t txbuf[5] = {0};
  //Setup the Command
  txbuf[0] = 0x97;
  txbuf[1] = 1;
  txbuf[2] = 0x00;

  D_XCAM_SetCRC(txbuf, 5);
  if (D_XCAM_DEBUG)
    fprintf(PAYLOAD, "Sending SPI Download Command\n\r");
  if (D_XCAM_transmit(txbuf, 5))
    return 1;
  osDelay(3);
  
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
  SPI_ret = HAL_SPI_Receive(&hspi1, buffer, 260, 3000);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
  
  if ((SPI_ret == HAL_OK) && (D_XCAM_HALOK))
    fprintf(PAYLOAD,"\tSPI_Receive return was HAL_OK\n\r");
  if (SPI_ret != HAL_OK)
  {
    fprintf(PAYLOAD,"\tSPI_Receive return was NOT HAL_OK\n\r");
    HAL_Recovery_Tree(SPI_ret);
    return 2;
  }

  if (D_XCAM_receive(txbuf, 0, true))  // it doesn't matter which buffer
    return 3;

  if (D_XCAM_ValidateCRC(buffer, 260) == false)
    fprintf(PAYLOAD, "WARNING: response failed CRC check\n\r");
  D_XCAM_PrintACKOrResponse(buffer, 260);
  return 0;
}


/**
 * @brief : This will pull a single image packet from the XCAM
 * @param buffer : Pointer to the buffer
 * @return (1) if it did not transmit (2) issue with HAL (3) if it did not receive (0)everything Went Well
 */ 
uint8_t D_XCAM_GetImageI2C(uint8_t *buffer){

  uint8_t txbuf[4] = {0};

  txbuf[0] = 0x95;
  txbuf[1] = 1;

  D_XCAM_SetCRC(txbuf, 4);
  if (D_XCAM_DEBUG)
    fprintf(PAYLOAD, "Sending I2C Download Image Command\n\r");
  if (D_XCAM_transmit(txbuf, 4))
    return 1;
  osDelay(3);
  if (D_XCAM_receive(buffer, 260, true))
    return 2;

  D_XCAM_PrintACKOrResponse(buffer, 260);
  int len = 260;
  
  return 0;
}


/**
 * @brief : This will Send an initializiation command to XCAM
 * @param none
 * @return (1) if it did not transmit (2) issue with HAL (0)everything Went Well
 */ 
uint8_t D_XCAM_SendInitCommand(void)
{
  return D_XCAM_SendInitOrUpdate(true, false);
}

/**
 * @brief : This will Send an Enable Imaging command to XCAM
 * @param none
 * @return (1) if it did not transmit (2) issue with HAL (0)everything Went Well
 */ 
uint8_t D_XCAM_EnableImagingMode(void)
{
  D_XCAM_SetParameter(XCAM_SENSOR,XCAM_sensor);
  D_XCAM_SetParameter(XCAM_TIMEOUT,XCAM_timeout);
  D_XCAM_SetParameter(XCAM_TIMEOUT, XCAM_timeout);
  D_XCAM_SetParameter(XCAM_WINDOW, XCAM_window);
  D_XCAM_SetParameter(XCAM_INTTIMEFRAC, XCAM_inttimefrac);
  D_XCAM_SetParameter(XCAM_COMPRESS, XCAM_compress);
  D_XCAM_SetParameter(XCAM_THUMB, XCAM_thumb);
  return D_XCAM_SendInitOrUpdate(false, true);
}

/**
 * @brief : Send an initializiation or Update Command to XCAM
 * @param init : true for init, false for update
 * @param imagingmode : true for imaging, false for non-imaging
 * @return (1) if it did not transmit (2) issue with HAL (0)everything Went Well
 */ 
uint8_t D_XCAM_SendInitOrUpdate(bool init, bool imagingmode)
{
  uint8_t txbuf[25] = {0};
  uint8_t rxbuf[5] = {0};
  unsigned long ukube_time;

  if (init)
  {
    if (D_XCAM_DEBUG)
      fprintf(PAYLOAD, "Sending Init Command\n\r");
    txbuf[0] = 0x90;
  }
  else
  {
    if (D_XCAM_DEBUG)
      fprintf(PAYLOAD, "Sending Update Command");
    txbuf[0] = 0x92;
  }
  txbuf[1] = 1;
  if (imagingmode)
  {
    if (D_XCAM_DEBUG)
      fprintf(PAYLOAD, " and setting imaging mode.\n\r");
    txbuf[2] = 1;
  }
  else
  {
    if (D_XCAM_DEBUG)
      fprintf(PAYLOAD, " and setting standby mode.\n\r");
    txbuf[2] = 0;
  }
  
  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;
  HAL_RTC_GetTime(&hrtc,&sTime,calendar_format); // must be before GetDate
  HAL_RTC_GetDate(&hrtc,&sDate,calendar_format);
  fprintf(PAYLOAD,"Setting XCAM time to %02d-%02d %02d:%02d:%02d\n\r",
          sDate.Month,sDate.Date,sTime.Hours,sTime.Minutes,sTime.Seconds);

  ukube_time =   (long)sTime.Seconds
               +((long)sTime.Minutes*60L)
               +((long)sTime.Hours*3600L)
               +((long)sDate.Date*24L*3600L)
               +((long)sDate.Month*30L*24L*3600L);
               
  txbuf[5] = (uint8_t)((ukube_time >> 24) & 0xff);
  txbuf[6] = (uint8_t)((ukube_time >> 16) & 0xff);
  txbuf[7] = (uint8_t)((ukube_time >>  8) & 0xff);
  txbuf[8] = (uint8_t)((ukube_time      ) & 0xff);

  D_XCAM_SetCRC(txbuf, 25);
  if (D_XCAM_transmit(txbuf, 25))
    return 1;
  osDelay(3);
  if (D_XCAM_receive(rxbuf, 5, false))
    return 2;

  D_XCAM_PrintACKOrResponse(rxbuf, 5);
  return 0;
}

/****************************************************************
 * @brief : This will Send a command to XCAM to set a given Parameter
 * @param ID : Parameter to set
 * @param value : Value to set it to
 * @return (1) if it did not transmit (2) issue it ded not recieve (0)everything Went Well
 */
uint8_t D_XCAM_SetParameter(uint8_t ID, uint16_t value)
{
  uint8_t txbuf[7] = {0};
  uint8_t rxbuf[5] = {0};

  txbuf[0] = 0x93;
  txbuf[1] = 1;
  txbuf[2] = ID;
  txbuf[3] = (uint8_t) ((value >> 8) & 0xff);
  txbuf[4] = (uint8_t) (value & 0xff);

  D_XCAM_SetCRC(txbuf, 7);
  if (D_XCAM_DEBUG)
    fprintf(PAYLOAD, "Setting parameter %02x to %d.\n\r", ID, value);

  if (D_XCAM_transmit(txbuf, 7)){
      fprintf(PAYLOAD, "Could not Transmit to XCAM\n\r");
      return 1;
  }
    osDelay(3);
  if (D_XCAM_receive(rxbuf, 5, false)){
      fprintf(PAYLOAD, "Could not recieve from XCAM\n\r");
      return 2;
  }
  D_XCAM_PrintACKOrResponse(rxbuf, 5);
  return 0;
}

/****************************************************************
 * @brief : This will Send a command to XCAM to set a given Parameter
 * @param ID : Parameter to set
 * @return (1) if it did not transmit (2) issue it ded not recieve (0)everything Went Well
 */
const uint8_t * D_XCAM_GetParameter(uint8_t ID){
  uint8_t i;
  uint8_t txbuf[5] = {0};
  uint8_t status[22] = {0};

  txbuf[0] = 0x94;
  txbuf[1] = 1;
  txbuf[2] = ID;
  D_XCAM_SetCRC(txbuf, 5);

  for (i=0;i<22;i++)
    status[i] = 0;
  #ifdef DEBUG
    fprintf(PAYLOAD, "Requesting camera error.\n\r");
  #endif
  
  if (D_XCAM_transmit(txbuf, 5)){
      #ifdef DEBUG
        fprintf(PAYLOAD,"Could not transmit\r\n");
      #endif
  }

  osDelay(3);
  
  if (D_XCAM_receive(status, 22, true)){
    #ifdef DEBUG
      fprintf(PAYLOAD,"Could not receive\r\n");
    #endif
  }

  if (D_XCAM_RAWSTATUS){
    fprintf(PAYLOAD, "Status: 0x");
    for (i=0; i<22; i++)
      fprintf(PAYLOAD, "%02x ", status[i]);
    fprintf(PAYLOAD, "\r\n");
  }
  return status;
}


void D_XCAM_SetCRC(uint8_t* data, size_t len)
// length of total packet including CRC
{
  uint16_t crc_raw;
  crc_raw = D_XCAM_crc16(65535, data, len-2);
  data[len-2] = (uint8_t)((crc_raw >> 8) & 0xff);
  data[len-1] = (uint8_t)( crc_raw       & 0xff);
}

bool D_XCAM_ValidateCRC(uint8_t* data, size_t len)
// length of total packet including CRC
{
  uint16_t crc_raw;
  uint8_t crc1, crc2;
  crc_raw = D_XCAM_crc16(65535, data, (len-2));
  crc1 = (uint8_t)((crc_raw >> 8) & 0xff);
  crc2 = (uint8_t)( crc_raw       & 0xff);
  if ((data[len-2] == crc1) && 
      (data[len-1] == crc2))
    return true;
  return false;
}


uint16_t D_XCAM_crc16(uint16_t seed, uint8_t *pBuffer, int length)
{
  uint16_t crc_lut[256] = {
      0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
      0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
      0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
      0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
      0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
      0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
      0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
      0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
      0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823, 
      0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b, 
      0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12, 
      0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a, 
      0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 
      0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49, 
      0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70, 
      0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78, 
      0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f, 
      0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067, 
      0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e, 
      0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256, 
      0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d, 
      0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 
      0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c, 
      0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634, 
      0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab, 
      0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3, 
      0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a, 
      0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92, 
      0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 
      0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1, 
      0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8, 
      0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0};

  uint16_t crc = 65535;
  uint16_t data, temp, temp_rs, temp_xor, temp_ls, p;

  for (p=0; p<length; p++)
  {
    data     = pBuffer[p];
    temp_rs  = crc >> 8;
    temp_xor = temp_rs ^ data;
    temp     = temp_xor & 0xff;
    temp_ls  = crc << 8;
    crc      = crc_lut[temp] ^ temp_ls;
  }
  return crc;
}


uint8_t D_XCAM_transmit(uint8_t *buffer, size_t len)
{
  HAL_StatusTypeDef ret = HAL_ERROR;
  ret = HAL_I2C_Master_Transmit(&hi2c3, D_XCAM_ADDRESS << 1,
                                buffer, len, 100);
  if ((ret == HAL_OK) && D_XCAM_HALOK)
    fprintf(PAYLOAD,"\tXCAM_transmit return was HAL_OK\n\r");
  if (ret != HAL_OK)
  {
    fprintf(PAYLOAD,"\tXCAM_transmit return was NOT HAL_OK\n\r");
    fprintf(PAYLOAD, "\tAttempting to recover HAL");
    HAL_Recovery_Tree(ret);
    //Jump to HAL recovery
    return ret;
  }
  return 0;
}


uint8_t D_XCAM_receive(uint8_t *buffer, size_t len, bool ack)
{
  fprintf(PAYLOAD, "\tXCAM_receive\n\r");
  HAL_StatusTypeDef ret = HAL_ERROR;
  uint8_t buf[5] = {0};
  if (len > 0)
  {
    ret = HAL_I2C_Master_Receive(&hi2c3, D_XCAM_ADDRESS << 1,
                                 buffer, len, 100);
    if ((ret == HAL_OK) && D_XCAM_HALOK)
      fprintf(PAYLOAD,"\tXCAM_receive return was HAL_OK\n\r");
    if (ret != HAL_OK)
    {
      fprintf(PAYLOAD,"\tXCAM_receive return was NOT HAL_OK\n\r");
      HAL_Recovery_Tree(ret);
      return ret;
    }
    
    // there's a bug in the camera firmware where a command 0x95 returns a 0x91 as the first byte    
    if ((len == 260) && (buffer[0] = 0x91))
        buffer[0] = 0x95;
    if (D_XCAM_ValidateCRC(buffer, len) == false)
      fprintf(PAYLOAD, "WARNING: response failed CRC check\n\r");
  }
  //For Type 2 PackagesWe need to Send an Ack
  if (ack) // if we need to send an acknowledgement packet
  {
    buf[0] = buffer[0];
    buf[1] = 1;
    buf[2] = 0x7E;

    D_XCAM_SetCRC(buf, 5);
    ret = HAL_I2C_Master_Transmit(&hi2c3, D_XCAM_ADDRESS << 1,
                                  buf, 5, 100);
    if ((ret == HAL_OK) && D_XCAM_HALOK)
      fprintf(PAYLOAD,"\tACK return was HAL_OK\n\r");
    if (ret != HAL_OK)
    {
      fprintf(PAYLOAD,"\tACK return was NOT HAL_OK\n\r");
      HAL_Recovery_Tree(ret);
      return 2;
    }
  }
  return 0;
}


void D_XCAM_WaitSeconds(uint16_t numSeconds, bool verbose)
{
  uint16_t ii;
  if (verbose)
    fprintf(PAYLOAD,"Waiting %d seconds...",numSeconds);
  TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);
  for (ii=0; ii<(10*numSeconds); ii++)
  {
    osDelay(100);
    TaskMonitor_IamAlive(TASK_MONITOR_DEFAULT);
    if ((verbose) && ((ii%10) == 0))
      fprintf(PAYLOAD,"--%d--",ii/10);
  }
  if (verbose)
    fprintf(PAYLOAD,"\n\r");

}


void D_XCAM_PrintACKOrResponse(uint8_t *buffer, size_t len)
{
    uint16_t i;
  if ((len == 5) && (buffer[2] == 0x7e))
  {
    fprintf(PAYLOAD, "Response: ACK\n\r");
  }
  else
  {
    #ifdef DEBUG
      fprintf(PAYLOAD, "Response: 0x");
      for (i=0; i<len; i++)
          fprintf(PAYLOAD, "%02x ", buffer[i]);
      fprintf(PAYLOAD, "\n\r");
    #endif
  }
}
