/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2013 Semtech

Description: LoRaMac classC device implementation

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis and Gregory Cristian
*/
#include <string.h>
#include <math.h>
#include "board.h"

#include "LoRaMac.h"

/*!
 * When set to 1 the application uses the Over-the-Air activation procedure
 * When set to 0 the application uses the Personalization activation procedure
 */
#define OVER_THE_AIR_ACTIVATION                     0

/*!
 * Indicates if the end-device is to be connected to a private or public network
 */
#define LORAWAN_PUBLIC_NETWORK                      true

#if( OVER_THE_AIR_ACTIVATION != 0 )

/*!
 * Join requests trials duty cycle.
 */
#define OVER_THE_AIR_ACTIVATION_DUTYCYCLE           10000000  // 10 [s] value in us

/*!
 * Mote device IEEE EUI
 *
 * \remark must be written as a little endian value (reverse order of normal reading)
 *
 * \remark In this application the value is automatically generated by calling
 *         BoardGetUniqueId function
 */
#define LORAWAN_DEVICE_EUI                          { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

/*!
 * Application IEEE EUI
 *
 * \remark must be written as a little endian value (reverse order of normal reading)
 */
#define LORAWAN_APPLICATION_EUI                     { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

/*!
 * AES encryption/decryption cipher application key
 */
#define LORAWAN_APPLICATION_KEY                     { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00 }

#else

/*!
 * Current network ID
 */
#define LORAWAN_NETWORK_ID                          ( uint32_t )0

/*!
 * Device address on the network
 *
 * \remark must be written as a big endian value (normal reading order)
 *
 * \remark In this application the value is automatically generated using
 *         a pseudo random generator seeded with a value derived from
 *         BoardUniqueId value
 */
#define LORAWAN_DEVICE_ADDRESS                      ( uint32_t )0x00000000

/*!
 * AES encryption/decryption cipher network session key
 */
#define LORAWAN_NWKSKEY                             { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }

/*!
 * AES encryption/decryption cipher application session key
 */
#define LORAWAN_APPSKEY                             { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }

#endif

/*!
 * Defines the application data transmission duty cycle
 */
#define APP_TX_DUTYCYCLE                            5000000  // 5 [s] value in us
#define APP_TX_DUTYCYCLE_RND                        1000000  // 1 [s] value in us

/*!
 * LoRaWAN confirmed messages
 */
#define LORAWAN_CONFIRMED_MSG_ON                    false

/*!
 * LoRaWAN Adaptative Data Rate
 *
 * \remark Please note that when ADR is enabled the end-device should be static
 */
#define LORAWAN_ADR_ON                              1

/*!
 * LoRaWAN ETSI duty cycle control enable/disable
 *
 * \remark Please note that ETSI mandates duty cycled transmissions. Use only for test purposes
 */
#define LORAWAN_DUTYCYCLE_ON                        true

/*!
 * LoRaWAN application port
 */
#define LORAWAN_APP_PORT                            2

/*!
 * User application data buffer size
 */
#define LORAWAN_APP_DATA_SIZE                       16

#if( OVER_THE_AIR_ACTIVATION != 0 )

static uint8_t DevEui[] = LORAWAN_DEVICE_EUI;
static uint8_t AppEui[] = LORAWAN_APPLICATION_EUI;
static uint8_t AppKey[] = LORAWAN_APPLICATION_KEY;

#else

static uint8_t NwkSKey[] = LORAWAN_NWKSKEY;
static uint8_t AppSKey[] = LORAWAN_APPSKEY;

/*!
 * Device address
 */
static uint32_t DevAddr;

#endif

/*!
 * Indicates if the MAC layer has already joined a network.
 */
static bool IsNetworkJoined = false;

/*!
 * Application port
 */
static uint8_t AppPort = LORAWAN_APP_PORT;

/*!
 * User application data size
 */
static uint8_t AppDataSize = LORAWAN_APP_DATA_SIZE;

/*!
 * User application data buffer size
 */
#define LORAWAN_APP_DATA_MAX_SIZE                           64

/*!
 * User application data
 */
static uint8_t AppData[LORAWAN_APP_DATA_MAX_SIZE];


/*!
 * Indicates if the node is sending confirmed or unconfirmed messages
 */
static uint8_t IsTxConfirmed = LORAWAN_CONFIRMED_MSG_ON;

/*!
 * Defines the application data transmission duty cycle
 */
static uint32_t TxDutyCycleTime;

static TimerEvent_t TxNextPacketTimer;

#if( OVER_THE_AIR_ACTIVATION != 0 )

/*!
 * Defines the join request timer
 */
static TimerEvent_t JoinReqTimer;

#endif

/*!
 * Indicates if a new packet can be sent
 */
static bool TxNextPacket = true;
static bool ScheduleNextTx = false;
static bool DownlinkStatusUpdate = false;

static bool AppLedStateOn = false;

static LoRaMacCallbacks_t LoRaMacCallbacks;

static TimerEvent_t Led1Timer;
volatile bool Led1StateChanged = false;
static TimerEvent_t Led2Timer;
volatile bool Led2StateChanged = false;

volatile bool Led3StateChanged = false;

/*!
 * Prepares the frame buffer to be sent
 */
static void PrepareTxFrame( uint8_t port )
{
    switch( port )
    {
    case 2:
        {
            uint16_t pressure = 0;
            int16_t altitudeBar = 0;
            int16_t temperature = 0;
            int32_t latitude, longitude = 0;
            uint16_t altitudeGps = 0xFFFF;
            uint8_t batteryLevel = 0;

            pressure = ( uint16_t )( MPL3115ReadPressure( ) / 10 );             // in hPa / 10
            temperature = ( int16_t )( MPL3115ReadTemperature( ) * 100 );       // in �C * 100
            altitudeBar = ( int16_t )( MPL3115ReadAltitude( ) * 10 );           // in m * 10
            batteryLevel = BoardGetBatteryLevel( );                             // 1 (very low) to 254 (fully charged)
            GpsGetLatestGpsPositionBinary( &latitude, &longitude );
            altitudeGps = GpsGetLatestGpsAltitude( );                           // in m

            AppData[0] = AppLedStateOn;
            AppData[1] = ( pressure >> 8 ) & 0xFF;
            AppData[2] = pressure & 0xFF;
            AppData[3] = ( temperature >> 8 ) & 0xFF;
            AppData[4] = temperature & 0xFF;
            AppData[5] = ( altitudeBar >> 8 ) & 0xFF;
            AppData[6] = altitudeBar & 0xFF;
            AppData[7] = batteryLevel;
            AppData[8] = ( latitude >> 16 ) & 0xFF;
            AppData[9] = ( latitude >> 8 ) & 0xFF;
            AppData[10] = latitude & 0xFF;
            AppData[11] = ( longitude >> 16 ) & 0xFF;
            AppData[12] = ( longitude >> 8 ) & 0xFF;
            AppData[13] = longitude & 0xFF;
            AppData[14] = ( altitudeGps >> 8 ) & 0xFF;
            AppData[15] = altitudeGps & 0xFF;
        }
        break;
    default:
        break;
    }
}

static void ProcessRxFrame( LoRaMacEventFlags_t *flags, LoRaMacEventInfo_t *info )
{
    switch( info->RxPort ) // Check Rx port number
    {
    case 1: // The application LED can be controlled on port 1 or 2
    case 2:
        if( info->RxBufferSize == 1 )
        {
            AppLedStateOn = info->RxBuffer[0] & 0x01;
            Led3StateChanged = true;
        }
        break;
    default:
        break;
    }
}

static bool SendFrame( void )
{
    uint8_t sendFrameStatus = 0;

    if( IsTxConfirmed == false )
    {
        sendFrameStatus = LoRaMacSendFrame( AppPort, AppData, AppDataSize );
    }
    else
    {
        sendFrameStatus = LoRaMacSendConfirmedFrame( AppPort, AppData, AppDataSize, 8 );
    }

    switch( sendFrameStatus )
    {
    case 5: // NO_FREE_CHANNEL
        // Try again later
        return true;
    default:
        return false;
    }
}

#if( OVER_THE_AIR_ACTIVATION != 0 )

/*!
 * \brief Function executed on JoinReq Timeout event
 */
static void OnJoinReqTimerEvent( void )
{
    TimerStop( &JoinReqTimer );
    TxNextPacket = true;
}

#endif

/*!
 * \brief Function executed on TxNextPacket Timeout event
 */
static void OnTxNextPacketTimerEvent( void )
{
    TimerStop( &TxNextPacketTimer );
    TxNextPacket = true;
}

/*!
 * \brief Function executed on Led 1 Timeout event
 */
static void OnLed1TimerEvent( void )
{
    TimerStop( &Led1Timer );
    Led1StateChanged = true;
}

/*!
 * \brief Function executed on Led 2 Timeout event
 */
static void OnLed2TimerEvent( void )
{
    TimerStop( &Led2Timer );
    Led2StateChanged = true;
}

/*!
 * \brief Function to be executed on MAC layer event
 */
static void OnMacEvent( LoRaMacEventFlags_t *flags, LoRaMacEventInfo_t *info )
{
    if( flags->Bits.JoinAccept == 1 )
    {
#if( OVER_THE_AIR_ACTIVATION != 0 )
        TimerStop( &JoinReqTimer );
#endif
        IsNetworkJoined = true;
    }
    else
    {
        if( flags->Bits.Tx == 1 )
        {
        }

        if( flags->Bits.Rx == 1 )
        {
            if( flags->Bits.RxData == true )
            {
                ProcessRxFrame( flags, info );
            }
            
            DownlinkStatusUpdate = true;
            TimerStart( &Led2Timer );
        }
    }
    // Schedule a new transmission
    ScheduleNextTx = true;
}

/**
 * Main application entry point.
 */
int main( void )
{
#if( OVER_THE_AIR_ACTIVATION != 0 )
    uint8_t sendFrameStatus = 0;
#endif
    bool trySendingFrameAgain = false;

    BoardInitMcu( );
    BoardInitPeriph( );

    LoRaMacCallbacks.MacEvent = OnMacEvent;
    LoRaMacCallbacks.GetBatteryLevel = BoardGetBatteryLevel;
    LoRaMacInit( &LoRaMacCallbacks );

    IsNetworkJoined = false;

#if( OVER_THE_AIR_ACTIVATION == 0 )
    // Random seed initialization
    srand1( BoardGetRandomSeed( ) );
    // Choose a random device address based on Board unique ID
    // NwkAddr rand [0, 33554431]
    DevAddr = randr( 0, 0x01FFFFFF );

    LoRaMacInitNwkIds( LORAWAN_NETWORK_ID, DevAddr, NwkSKey, AppSKey );
    IsNetworkJoined = true;
#else
    // Initialize LoRaMac device unique ID
    BoardGetUniqueId( DevEui );

    // Sends a JoinReq Command every OVER_THE_AIR_ACTIVATION_DUTYCYCLE
    // seconds until the network is joined
    TimerInit( &JoinReqTimer, OnJoinReqTimerEvent ); 
    TimerSetValue( &JoinReqTimer, OVER_THE_AIR_ACTIVATION_DUTYCYCLE );
#endif

    TxNextPacket = true;
    TimerInit( &TxNextPacketTimer, OnTxNextPacketTimerEvent );
    
    TimerInit( &Led1Timer, OnLed1TimerEvent );
    TimerSetValue( &Led1Timer, 25000 );

    TimerInit( &Led2Timer, OnLed2TimerEvent );
    TimerSetValue( &Led2Timer, 25000 );

    LoRaMacSetAdrOn( LORAWAN_ADR_ON );
    LoRaMacTestSetDutyCycleOn( LORAWAN_DUTYCYCLE_ON );
    LoRaMacSetPublicNetwork( LORAWAN_PUBLIC_NETWORK );
    LoRaMacSetDeviceClass( CLASS_C );

    while( 1 )
    {
        while( IsNetworkJoined == false )
        {
#if( OVER_THE_AIR_ACTIVATION != 0 )
            if( TxNextPacket == true )
            {
                TxNextPacket = false;
                
                sendFrameStatus = LoRaMacJoinReq( DevEui, AppEui, AppKey );
                switch( sendFrameStatus )
                {
                case 1: // BUSY
                    break;
                case 0: // OK
                case 2: // NO_NETWORK_JOINED
                case 3: // LENGTH_PORT_ERROR
                case 4: // MAC_CMD_ERROR
                case 6: // DEVICE_OFF
                default:
                    // Relaunch timer for next trial
                    TimerStart( &JoinReqTimer );
                    break;
                }
            }
            TimerLowPowerHandler( );
#endif
        }

        if( Led1StateChanged == true )
        {
            Led1StateChanged = false;
            // Switch LED 1 OFF
            GpioWrite( &Led1, 1 );
        }
        if( Led2StateChanged == true )
        {
            Led2StateChanged = false;
            // Switch LED 2 OFF
            GpioWrite( &Led2, 1 );
        }
        if( Led3StateChanged == true )
        {
            Led3StateChanged = false;
            GpioWrite( &Led3, ( ( AppLedStateOn & 0x01 ) != 0 ) ? 0 : 1 );
        }
        if( DownlinkStatusUpdate == true )
        {
            DownlinkStatusUpdate = false;
            // Switch LED 2 ON for each received downlink
            GpioWrite( &Led2, 0 );
        }

        if( ScheduleNextTx == true )
        {
            ScheduleNextTx = false;

            // Schedule next packet transmission
            TxDutyCycleTime = APP_TX_DUTYCYCLE + randr( -APP_TX_DUTYCYCLE_RND, APP_TX_DUTYCYCLE_RND );
            TimerSetValue( &TxNextPacketTimer, TxDutyCycleTime );
            TimerStart( &TxNextPacketTimer );
        }

        if( trySendingFrameAgain == true )
        {
            trySendingFrameAgain = SendFrame( );
        }
        if( TxNextPacket == true )
        {
            TxNextPacket = false;
            
            PrepareTxFrame( AppPort );
            
            // Switch LED 1 ON
            GpioWrite( &Led1, 0 );
            TimerStart( &Led1Timer );
            
            trySendingFrameAgain = SendFrame( );
        }

        TimerLowPowerHandler( );
    }
}
