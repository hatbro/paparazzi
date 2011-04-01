/*
 * Copyright (C) 2011 The Paparazzi Team
 *
 * This file is part of paparazzi.
 *
 * paparazzi is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * paparazzi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with paparazzi; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*---------------------------------------------------------------------------
    Copyright (c)  Ryan Mechatronics 2008.  All Rights Reserved.

    File: *.c

    Description: CHIMU Protocol Parser
                 

    Public Functions:
      CHIMU_Init        Create component instance
      CHIMU_Done        Free component instance
      CHIMU_Parse       Parse the RX byte stream message

    Applicable Documents:
        CHIMU User Manual

    Adapted to paparazzi by C. De Wagter

---------------------------------------------------------------------------*/

#include "imu_chimu.h"
#include "string.h"
//#include "crc.h"
#include "endian_functions.h"
//#include "util.h"
#include "math.h"



//---[Defines]------------------------------------------------------
#define CHIMU_STATE_MACHINE_START   	0
#define CHIMU_STATE_MACHINE_HEADER2   	1
#define CHIMU_STATE_MACHINE_LEN		2
#define CHIMU_STATE_MACHINE_DEVICE	3
#define CHIMU_STATE_MACHINE_ID		4
#define CHIMU_STATE_MACHINE_PAYLOAD	5
#define CHIMU_STATE_MACHINE_XSUM	6


//---[DEFINES for Message List]---------------------------------------
//Message ID's that go TO the CHIMU
#define MSG00_PING		0x00
#define MSG01_BIAS		0x01
#define MSG02_DACMODE		0x02
#define MSG03_CALACC		0x03
#define MSG04_CALMAG		0x04
#define MSG05_CALRATE		0x05
#define MSG06_CONFIGCLR		0x06
#define MSG07_CONFIGSET		0x07
#define MSG08_SAVEGYROBIAS	0x08
#define MSG09_ESTIMATOR		0x09
#define MSG0A_SFCHECK		0x0A
#define MSG0B_CENTRIP		0x0B
#define MSG0C_INITGYROS		0x0C
#define MSG0D_DEVICEID		0x0D
#define MSG0E_REFVECTOR		0x0E
#define MSG0F_RESET		0x0F
#define MSG10_UARTSETTINGS	0x10
#define MSG11_SERIALNUMBER	0x11

//Output message identifiers from the CHIMU unit
//---[Defines]------------------------------------------------------
#define CHIMU_Msg_0_Ping			0
#define CHIMU_Msg_1_IMU_Raw                     1
#define CHIMU_Msg_2_IMU_FP			2
#define CHIMU_Msg_3_IMU_Attitude                3
#define CHIMU_Msg_4_BiasSF			4
#define CHIMU_Msg_5_BIT                         5
#define CHIMU_Msg_6_MagCal			6
#define CHIMU_Msg_7_GyroBias                    7
#define CHIMU_Msg_8_TempCal                     8
#define CHIMU_Msg_9_DAC_Offsets                 9
#define CHIMU_Msg_10_Res			10
#define CHIMU_Msg_11_Res			11
#define CHIMU_Msg_12_Res			12
#define CHIMU_Msg_13_Res			13
#define CHIMU_Msg_14_RefVector                  14
#define CHIMU_Msg_15_SFCheck                    15


//---[COM] defines
#define CHIMU_COM_ID_LOW	0x00
#define CHIMU_COM_ID_HIGH	0x1F  //Must set this to the max ID expected above

#define NP_MAX_CMD_LEN			8		// maximum command length (CHIMU address)
#define NP_MAX_DATA_LEN			256		// maximum data length
#define NP_MAX_CHAN			36		// maximum number of channels
#define NP_WAYPOINT_ID_LEN		32		// waypoint max string len
#define NP_XSUM_LEN                     3               // chars in checksum string

#define CHIMU_STANDARD   0x00



/*---------------------------------------------------------------------------
        Name: CHIMU_Init
 
---------------------------------------------------------------------------*/
void CHIMU_Init(CHIMU_PARSER_DATA   *pstData)
{
  unsigned char i;
  pstData->m_State = CHIMU_STATE_MACHINE_START;
  pstData->m_Checksum = 0x00;
  pstData->m_ReceivedChecksum = 0x00;
  pstData->m_Index = 0;
  pstData->m_PayloadIndex = 0;

  //Sensor data holder
  pstData->m_sensor.cputemp=0.0;
  for (i=0;i<3;i++)
  {
    pstData->m_sensor.acc[i]=0.0;
    pstData->m_sensor.rate[i]=0.0;
    pstData->m_sensor.mag[i]=0.0;
  }
  pstData->m_sensor.spare1=0.0;
  //Attitude data
  pstData->m_attitude.euler.phi = 0.0;
  pstData->m_attitude.euler.theta = 0.0;
  pstData->m_attitude.euler.psi = 0.0;
  //Attitude rate data
  pstData->m_attrates.euler.phi = 0.0;
  pstData->m_attrates.euler.theta = 0.0;
  pstData->m_attrates.euler.psi = 0.0;

  for (i=0; i<CHIMU_RX_BUFFERSIZE; i++) 
  {
    pstData->m_Payload[i]= 0x00;
    pstData->m_FullMessage[i]= 0x00;
  }
  pstData->m_MsgLen = 0;
  pstData->m_MsgID = 0;
  pstData->m_TempDeviceID =0;
  pstData->m_DeviceID = 0x01; //look at this later
}


/*---------------------------------------------------------------------------
        Name: CHIMU_Parse
    Abstract: Parse message, returns TRUE if new data.
        Note: A typical sentence is constructed as:
---------------------------------------------------------------------------*/

unsigned char CHIMU_Parse(
		    unsigned char btData,           /* input byte stream buffer */
		    unsigned char bInputType,       /* for future use if special builds of CHIMU data are performed */
		    CHIMU_PARSER_DATA   *pstData)   /* resulting data           */
{

    //long int       i;
    char           bUpdate = FALSE;

    switch (pstData->m_State) {
            case CHIMU_STATE_MACHINE_START:  // Waiting for start character 0xAE
                      if(btData == 0xAE)
                       {
                          pstData->m_State = CHIMU_STATE_MACHINE_HEADER2;
                          pstData->m_Index = 0;
                          pstData->m_FullMessage[pstData->m_Index++] = btData;
                       } else {
                          ;;
                       }
                      bUpdate = FALSE;
                      break;
            case CHIMU_STATE_MACHINE_HEADER2:  // Waiting for second header character 0xAE
                      if(btData == 0xAE) 
                      { 
                          pstData->m_State = CHIMU_STATE_MACHINE_LEN;
                          pstData->m_FullMessage[pstData->m_Index++]=btData;
                      } else {
                          pstData->m_State = CHIMU_STATE_MACHINE_START;
                      } //Fail to see header.  Restart.
                      break;
            case CHIMU_STATE_MACHINE_LEN:  // Get chars to read
              if( btData <= CHIMU_RX_BUFFERSIZE) 
                      { 
                        pstData->m_MsgLen = btData  ; // It might be invalid, but we do a check on buffer size
                        pstData->m_FullMessage[pstData->m_Index++]=btData;
                        pstData->m_State = CHIMU_STATE_MACHINE_DEVICE;
                      } else {
                        pstData->m_State = CHIMU_STATE_MACHINE_START; //Length byte exceeds buffer.  Signal a fail and restart
                        //BuiltInTest(BIT_COM_UART_RECEIPTFAIL, BIT_FAIL);
                      }
              break;
            case CHIMU_STATE_MACHINE_DEVICE:  // Get device.  If not us, ignore and move on.  Allows common com with Monkey / Chipmunk
              if( (btData == pstData->m_DeviceID) || (btData == 0xAA)) 
                      { //0xAA is global message
                        pstData->m_TempDeviceID = btData;
                        pstData->m_FullMessage[pstData->m_Index++]=btData;
                        pstData->m_State = CHIMU_STATE_MACHINE_ID;
                      } else {
                        pstData->m_State = CHIMU_STATE_MACHINE_START;
                      } //Fail to see correct device ID.  Restart.
              break;
            case CHIMU_STATE_MACHINE_ID:  // Get ID
                      pstData->m_MsgID = btData; // might be invalid, chgeck it out here:
                      if ( (pstData->m_MsgID<CHIMU_COM_ID_LOW) || (pstData->m_MsgID>CHIMU_COM_ID_HIGH)) 
                      { 
                        pstData->m_State = CHIMU_STATE_MACHINE_START;
                        //BuiltInTest(BIT_COM_UART_RECEIPTFAIL, BIT_FAIL);
                      } else {
                        pstData->m_FullMessage[pstData->m_Index++]=btData;
                        pstData->m_PayloadIndex = 0;
                        pstData->m_State = CHIMU_STATE_MACHINE_PAYLOAD; //Finally....Good to go...
                      }
              break;
            case CHIMU_STATE_MACHINE_PAYLOAD:  // Waiting for number of bytes in payload
                      pstData->m_Payload[pstData->m_PayloadIndex++]= btData;
                      pstData->m_FullMessage[pstData->m_Index++]=btData;
                      if ((pstData->m_Index) >= (pstData->m_MsgLen + 5)) //Now we have the payload.  Verify XSUM and then parse it next
                      {
// TODO Redo Checksum
//                        pstData->m_Checksum = (unsigned char) ((UpdateCRC(0xFFFFFFFF , pstData->m_FullMessage , (unsigned long) (pstData->m_MsgLen)+5)) & 0xFF);                                               
                        pstData->m_State = CHIMU_STATE_MACHINE_XSUM;
                      } else {
                        return FALSE;
                      }
                      break;
            case CHIMU_STATE_MACHINE_XSUM:  // Verify
                      pstData->m_ReceivedChecksum = btData;
                      pstData->m_FullMessage[pstData->m_Index++]=btData;
                      if (pstData->m_Checksum!=pstData->m_ReceivedChecksum) 
                      {
                        bUpdate = FALSE;
                        //BuiltInTest(BIT_COM_UART_RECEIPTFAIL, BIT_FAIL);
                      } else {
                        //Xsum passed, go parse it.
                        // We have pstData->m_MsgID to parse off of, pstData->m_pstData->m_Payload as the data.
                        bUpdate = CHIMU_ProcessMessage(&pstData->m_MsgID, pstData->m_Payload, pstData);
                      }
                      pstData->m_State = CHIMU_STATE_MACHINE_START;
                      break;
                default:
                      pstData->m_State = CHIMU_STATE_MACHINE_START;
              } /* End of SWITCH */
  return (bUpdate);
}


///////////////////////////////////////////////////////////////////////////////
// Process CHIMU sentence - Use the CHIMU address (*pCommand) and call the
// appropriate sentence data processor.
///////////////////////////////////////////////////////////////////////////////

unsigned char CHIMU_ProcessMessage(unsigned char *pMsgID, unsigned char *pPayloadData, CHIMU_PARSER_DATA *pstData)
{
    //Msgs from CHIMU are off limits (i.e.any CHIMU messages sent up the uplink should go to 
    //CHIMU).  

    //Any CHIMU messages coming from the ground should be ignored, as that byte stream goes up to CHIMU
    // by itself.  However, here we should decode CHIMU messages being received and 
    //  a) pass them down to ground
    //  b) grab the data from the CHIMU for our own needs / purposes
    int CHIMU_index =0; 
    float sanity_check=0.0;

	switch (pstData->m_MsgID){
		case CHIMU_Msg_0_Ping:
                  CHIMU_index = 0;
                  gCHIMU_SW_Exclaim = pPayloadData[CHIMU_index]; CHIMU_index++;
                  gCHIMU_SW_Major = pPayloadData[CHIMU_index]; CHIMU_index++;
                  gCHIMU_SW_Minor = pPayloadData[CHIMU_index]; CHIMU_index++;
                  gCHIMU_SW_SerialNumber = (pPayloadData[CHIMU_index]<<8) & (0x0000FF00); CHIMU_index++;
                  gCHIMU_SW_SerialNumber += pPayloadData[CHIMU_index]; CHIMU_index++;
                  return TRUE;
                  break;
		case CHIMU_Msg_1_IMU_Raw:
                  break;
		case CHIMU_Msg_2_IMU_FP:
                  CHIMU_index = 0;
                  memmove (&pstData->m_sensor.cputemp, &pPayloadData[CHIMU_index], sizeof(pstData->m_sensor.cputemp));CHIMU_index += (sizeof(pstData->m_sensor.cputemp));
                  pstData->m_sensor.cputemp = FloatSwap(pstData->m_sensor.cputemp);
                  memmove (&pstData->m_sensor.acc[0], &pPayloadData[CHIMU_index], sizeof(pstData->m_sensor.acc));CHIMU_index += (sizeof(pstData->m_sensor.acc));
                  pstData->m_sensor.acc[0] = FloatSwap(pstData->m_sensor.acc[0]);
                  pstData->m_sensor.acc[1] = FloatSwap(pstData->m_sensor.acc[1]);
                  pstData->m_sensor.acc[2] = FloatSwap(pstData->m_sensor.acc[2]);
                  memmove (&pstData->m_sensor.rate[0], &pPayloadData[CHIMU_index], sizeof(pstData->m_sensor.rate));CHIMU_index += (sizeof(pstData->m_sensor.rate));
                  pstData->m_sensor.rate[0] = FloatSwap(pstData->m_sensor.rate[0]);
                  pstData->m_sensor.rate[1] = FloatSwap(pstData->m_sensor.rate[1]);
                  pstData->m_sensor.rate[2] = FloatSwap(pstData->m_sensor.rate[2]); 
                  memmove (&pstData->m_sensor.mag[0], &pPayloadData[CHIMU_index], sizeof(pstData->m_sensor.mag));CHIMU_index += (sizeof(pstData->m_sensor.mag));
                  pstData->m_sensor.mag[0] = FloatSwap(pstData->m_sensor.mag[0]);
                  pstData->m_sensor.mag[1] = FloatSwap(pstData->m_sensor.mag[1]);
                  pstData->m_sensor.mag[2] = FloatSwap(pstData->m_sensor.mag[2]);
                  memmove (&pstData->m_sensor.spare1, &pPayloadData[CHIMU_index], sizeof(pstData->m_sensor.spare1));CHIMU_index += (sizeof(pstData->m_sensor.spare1));
                  pstData->m_sensor.spare1 = FloatSwap(pstData->m_sensor.spare1);
                  return TRUE;
                  break;
		case CHIMU_Msg_3_IMU_Attitude:
                  //Attitude message data from CHIMU
                  // Includes attitude and rates only, along with configuration bits
                  // All you need for control

                  //Led_On(LED_RED);

                  CHIMU_index = 0;
                  memmove (&pstData->m_attitude.euler, &pPayloadData[CHIMU_index], sizeof(pstData->m_attitude.euler));CHIMU_index += sizeof(pstData->m_attitude.euler);
                  pstData->m_attitude.euler.phi = FloatSwap(pstData->m_attitude.euler.phi);
                  pstData->m_attitude.euler.theta = FloatSwap(pstData->m_attitude.euler.theta);
                  pstData->m_attitude.euler.psi = FloatSwap(pstData->m_attitude.euler.psi);
                  memmove (&pstData->m_sensor.rate[0], &pPayloadData[CHIMU_index], sizeof(pstData->m_sensor.rate));CHIMU_index += (sizeof(pstData->m_sensor.rate));
                  pstData->m_sensor.rate[0] = FloatSwap(pstData->m_sensor.rate[0]);
                  pstData->m_sensor.rate[1] = FloatSwap(pstData->m_sensor.rate[1]);
                  pstData->m_sensor.rate[2] = FloatSwap(pstData->m_sensor.rate[2]);

                  memmove (&pstData->m_attitude.q, &pPayloadData[CHIMU_index], sizeof(pstData->m_attitude.q));CHIMU_index += sizeof(pstData->m_attitude.q);
                  pstData->m_attitude.q.s = FloatSwap(pstData->m_attitude.q.s);
                  pstData->m_attitude.q.v.x = FloatSwap(pstData->m_attitude.q.v.x);
                  pstData->m_attitude.q.v.y = FloatSwap(pstData->m_attitude.q.v.y);
                  pstData->m_attitude.q.v.z = FloatSwap(pstData->m_attitude.q.v.z);

                  memmove (&pstData->m_attrates.q, &pPayloadData[CHIMU_index], sizeof(pstData->m_attrates.q));CHIMU_index += sizeof(pstData->m_attitude.q);
                  pstData->m_attrates.q.s = FloatSwap(pstData->m_attrates.q.s);
                  pstData->m_attrates.q.v.x = FloatSwap(pstData->m_attrates.q.v.x);
                  pstData->m_attrates.q.v.y = FloatSwap(pstData->m_attrates.q.v.y);
                  pstData->m_attrates.q.v.z = FloatSwap(pstData->m_attrates.q.v.z);

                  //Now put the rates into the Euler section as well.  User can use pstData->m_attitude and pstData->m_attrates structures for control
                  pstData->m_attrates.euler.phi = pstData->m_sensor.rate[0];
                  pstData->m_attrates.euler.theta = pstData->m_sensor.rate[1];
                  pstData->m_attrates.euler.psi = pstData->m_sensor.rate[2];

/*
	// TODO: Read configuration bits

                  gCalStatus = pPayloadData[CHIMU_index]; CHIMU_index ++;
                  gCHIMU_BIT = pPayloadData[CHIMU_index]; CHIMU_index ++;

                  gConfigInfo = pPayloadData[CHIMU_index]; CHIMU_index ++;
                  bC0_SPI_En = BitTest (gConfigInfo, 0); 
                  bC1_HWCentrip_En = BitTest (gConfigInfo, 1); 
                  bC2_TempCal_En = BitTest (gConfigInfo, 2); 
                  bC3_RateOut_En = BitTest (gConfigInfo, 3); 
                  bC4_TBD = BitTest (gConfigInfo, 4); 
                  bC5_Quat_Est = BitTest (gConfigInfo, 5); 
                  bC6_SWCentrip_En = BitTest (gConfigInfo, 6); 
                  bC7_AllowHW_Override = BitTest (gConfigInfo, 7); 

                  //CHIMU currently (v 1.3) does not compute Eulers if quaternion estimator is selected
                  if(bC5_Quat_Est == TRUE)
                  {
                    pstData->m_attitude = GetEulersFromQuat((pstData->m_attitude));
                  }
*/

                  //NEW:  Checks for bad attitude data (bad SPI maybe?)
                  //      Only allow globals to contain updated data if it makes sense
                  sanity_check = (pstData->m_attitude.q.s * pstData->m_attitude.q.s);
                  sanity_check += (pstData->m_attitude.q.v.x * pstData->m_attitude.q.v.x);
                  sanity_check += (pstData->m_attitude.q.v.y * pstData->m_attitude.q.v.y);
                  sanity_check += (pstData->m_attitude.q.v.z * pstData->m_attitude.q.v.z);

                  if ((sanity_check > 0.8) && (sanity_check < 1.2)) //Should be 1.0 (normalized quaternion)
                  { 
//                    gAttitude = pstData->m_attitude;
//                    gAttRates = pstData->m_attrates;
//                    gSensor = pstData->m_sensor;
                  } else 
		  {
                    //TODO:  Log BIT that indicates IMU message incoming failed (maybe SPI error?)
                  }

                  //Led_Off(LED_RED);
 
                  return TRUE;
                  break;
		case CHIMU_Msg_4_BiasSF:
		case CHIMU_Msg_5_BIT:
		case CHIMU_Msg_6_MagCal:
		case CHIMU_Msg_7_GyroBias:
        	case CHIMU_Msg_8_TempCal:
		case CHIMU_Msg_9_DAC_Offsets:
		case CHIMU_Msg_10_Res:
		case CHIMU_Msg_11_Res:
		case CHIMU_Msg_12_Res:
		case CHIMU_Msg_13_Res:
		case CHIMU_Msg_14_RefVector:
		case CHIMU_Msg_15_SFCheck:
                  break;
		default:
                  return FALSE;
                  break;
	}
}

CHIMU_attitude_data GetEulersFromQuat(CHIMU_attitude_data attitude)
{
  CHIMU_attitude_data ps;
  ps = attitude;
  float x, sqw,sqx,sqy,sqz,norm;
  sqw = ps.q.s * ps.q.s;
  sqx = ps.q.v.x * ps.q.v.x;
  sqy = ps.q.v.y * ps.q.v.y;
  sqz = ps.q.v.z * ps.q.v.z;
  norm = sqrt(sqw + sqx + sqy + sqz);
  //Normalize the quat
  ps.q.s = ps.q.s / norm;
  ps.q.v.x = ps.q.v.x / norm;
  ps.q.v.y = ps.q.v.y / norm;
  ps.q.v.z = ps.q.v.z / norm;
  ps.euler.phi =atan2(2.0 * (ps.q.s * ps.q.v.x + ps.q.v.y * ps.q.v.z), (1 - 2 * (sqx + sqy)));
  if (ps.euler.phi < 0)  ps.euler.phi = ps.euler.phi + 2 *PI;
  x = ((2.0 * (ps.q.s * ps.q.v.y - ps.q.v.z * ps.q.v.x)));
  //Below needed in event normalization not done
  if (x > 1.0) x = 1.0;
  if (x < -1.0) x = -1.0;
  //
  if ((ps.q.v.x * ps.q.v.y + ps.q.v.z * ps.q.s) == 0.5) 
          {
          ps.euler.theta = 2 *atan2(ps.q.v.x, ps.q.s);
          }
          else
          if ((ps.q.v.x * ps.q.v.y + ps.q.v.z * ps.q.s) == -0.5) 
                  {
                  ps.euler.theta = -2 *atan2(ps.q.v.x, ps.q.s);
                  }
          else{
                  ps.euler.theta = asin(x);
                  }
  ps.euler.psi = atan2(2.0 * (ps.q.s * ps.q.v.z + ps.q.v.x * ps.q.v.y), (1 - 2 * (sqy + sqz)));
  if (ps.euler.psi < 0) 
          {
           ps.euler.psi = ps.euler.psi + (2 * PI);
          }

  return(ps);
  
}

