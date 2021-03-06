/* 
 *  2018/05/17 - Nicola Ariutti
 *  This sketch uses:
 *  - Sparkfun Monster Moto Shield;
 *  - Bourns 10K linear motorized fader;
 *  - Adafruit 12 keys MPR121 breakout board;
 *  
 *  CONNECTIONS
 *  Connect:
 *  - motor channel A to Sparkfun Monster Motor shield A1 output;
 *  - motor channel B to Sparkfun Monster Motor shield B1 output;
 *  - potentiometer pin 1 to Arduino 5V;
 *  - potentiometer pin 2 to Arduino A4 pin;
 *  - potentiometer pin 3 to Arduino GND
 *  - MPR121 board GND to Arduino GND;
 *  - MPR121 board VIN to Arduino 5V;
 *  - MPR121 board SDA to Arduino SDA;
 *  - MPR121 board SCL to Arduino SCL;
 *  - potentiometer pin T to MPR121 pin 0;
 *  
 *  TODO:
 *  - Add current sensing
 *  - Add polarity switching utilyti function
 *  
 */
 

// Master State Machine *************************************************************
enum {
  MASTER_TOUCH = 0,
  MASTER_WAIT4STANDBY,
  MASTER_SEEK
} master_state;

enum {
  SEEK_QUIET = 0,
  SEEK_FORWARD,
  SEEK_REVERSE
} seek_state;

// Motor / Potentiometer stuff ******************************************************
#include <TimerOne.h>
#include "Limulo_Motor.h"

Motor motor;
unsigned int timeGap = 10000;// in uSecs
unsigned int MOTORSPEED = 130; // use 160 if the motor need to drag an heavy load
volatile boolean motorStatus = true;
volatile unsigned int motorChangeCount = 0;
boolean motorStatusCopy;
unsigned int motorChangeCountCopy;

#define POT A4
int ADDR_POT = 15;
uint16_t pot, destination;
int STOP_LOW = 20;
int STOP_HIGH = 1000;
int DLY_TIME = 10;
// this is the minimum distance of the new choosen destination
int DISTANCE = 400;

/*
 * This function is building a sort of PWM
 * to further control the speed of the motor.
 * 
 * i.e. If you set the last number to 4, the
 * duty cycle will be of 25%.
 * 
 * ||___||___||___||___||___ (25% duty cycle)
*/
void swapMotorOnOff(void)
{
  if(motorChangeCount == 0) { 
    motorStatus = true;   
    motor.enable(1);
  }
  else if(motorChangeCount == 1) {
    motorStatus = false;
    motor.disable(1);
  }
  motorChangeCount ++;
  // you can change the last number to
  // shorten the duty cycle
  motorChangeCount = motorChangeCount%2;
}

// Touch stuff **********************************************************************
#include <Wire.h>
#include "Limulo_MPR121.h"
#define NPADS 1
#define FIRST_MPR_ADDR 0x5A
struct mpr121 {
  Limulo_MPR121 cap;
  uint8_t addr;
  // Save an history of the pads that have been touched/released
  uint16_t lasttouched = 0;
  uint16_t currtouched = 0;
  boolean bPadState[12];
} mpr;
bool touched = false;


// DEBUG FEATURE ********************************************************************
#define LED 13


/************************************************************************************
 * SETUP
 ***********************************************************************************/
 void setup()                         
{
  // Initiates the serial to do the monitoring
  Serial.begin(9600);  
  // wait until someone on the other side is ready to communicate.
  while(!Serial) { delay(10); }

  // motor stuff ********************************************************************
  motor.init();
  motor.enable(1);
  motor.disable(2);
  motor.setSpeed( MOTORSPEED );
  //Serial.println( motor.getSpeed() );
  //motor.forward();  
  motor.stop();

  //Serial.println("Time One initialization");
  Timer1.initialize( timeGap );
  Timer1.attachInterrupt(swapMotorOnOff);

  // touch stuff ********************************************************************
  mpr.cap = Limulo_MPR121();
  // mpr address can be one of these: 0x5A, 0x5B, 0x5C o 0x5D 
  mpr.addr = FIRST_MPR_ADDR;
  
  // Look for the MPR121 chip on the I2C bus
  if ( !mpr.cap.begin( mpr.addr ) ) {
    //Serial.println("MPR121 not found, check wiring?");
    while (1);
  }

  // initialize the array of booleans
  for(int j=0; j<12; j++) {
    mpr.bPadState[j] = false;
  }
  
  // possibly initialize the mpr with some initial settings
  //mpr.setRising( 1, 8, 1, 0);
  //mpr.setFalling( 1, 1, 2, 1);
  mpr.cap.setTouched( 1, 255, 255);
  //mpr.cap.setThresholds( 10, 6 );
  //mpr.setDebounces(1, 1);

  // state machine stuff ************************************************************
  master_state = MASTER_SEEK;
  seek_state = SEEK_QUIET;
  chooseNewDestination();

  // debug stuff ********************************************************************
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
}


/************************************************************************************
 * LOOP
 ***********************************************************************************/
 void loop() 
{
  getSerialData();

  // motor stuff ********************************************************************
  // copy the volatile variables in case we want to use them
  noInterrupts();
  motorStatusCopy = motorStatus;
  motorChangeCountCopy = motorChangeCount;
  interrupts();
  
  pot = analogRead(POT);

  Serial.print('p'); Serial.print(pot); Serial.println(';');

  // Touch stuff ********************************************************************
  // examine the state of the MPR
  mpr.currtouched = mpr.cap.touched(); // Get the currently touched pads
  touched = false;
  for(int j=0; j<NPADS; j++) // cycle through all the electrodes
  {
    if (( mpr.currtouched & _BV(j)) && !(mpr.lasttouched & _BV(j)) ) 
    {
      // pad 'j' has been touched
      //Serial.print("Pad "); Serial.print(j); Serial.println("\ttouched!");
      mpr.bPadState[j] = true;
      touched=true;
      digitalWrite(LED, mpr.bPadState[j]);
      Serial.print('t'); Serial.print(1); Serial.println(';');
    }
    if (!(mpr.currtouched & _BV(j)) && (mpr.lasttouched & _BV(j)) ) 
    {
      // pad 'i' has been released
      //Serial.print("Pad "); Serial.print(j); Serial.println("\treleased!");
      mpr.bPadState[j] = false;
      digitalWrite(LED, mpr.bPadState[j]);
      Serial.print('t'); Serial.print(0); Serial.println(';');
    }
  }
  mpr.lasttouched = mpr.currtouched; // save the state for the next cycle

  
  // Master state machine stuff *****************************************************
  if(master_state == MASTER_TOUCH)
  {
    //Serial.println("Debug: master touch case");
    if(!readTouch()) // check if there's a release
    {
      //motor.stop(); //redundant
      master_state = MASTER_WAIT4STANDBY;
      return; // exit the loop now!
    }
  }
  else if(master_state == MASTER_WAIT4STANDBY)
  {
    //Serial.println("Debug: master wait for standby case;");
    if(readTouch()) // check if there's a touch
    {
      motor.stop();
      master_state = MASTER_TOUCH;
      return; // exit the loop now!
    }
  } 
  else if(master_state == MASTER_SEEK)
  { 
    //Serial.println("Debug: master seek case;");
    if(readTouch()) // check if there's a touch
    {
      motor.stop();
      master_state = MASTER_TOUCH;
      return; // exit the loop now!
    }

    if( isArrived() ) {
      // we are almost arrived to the destination stop the motor and change state
      //Serial.println("Destination reached!");
      motor.stop();
      seek_state = SEEK_QUIET;
      master_state = MASTER_WAIT4STANDBY;
      // this code is executed only when we reach
      // the destination we selected.
      //seek_startWaitingTime = millis();
    } 
  } // end of MASTER_SEEK state

  delay(DLY_TIME); // so it is not overwhelming
}

/************************************************************************************
 * SERIAL UTILITY FUNCTIONS
 ***********************************************************************************/
void getSerialData()
{
  if(Serial.available()) {
    char user_input = Serial.read(); // Read user input and trigger appropriate function
      
    if (user_input =='1')  {
       motor.stop();
    }
    else if(user_input =='2')  {
      motor.forward();
    }
    else if(user_input =='3')  {
      motor.reverse();
    }
    else if(user_input =='+')  {
      motor.increaseSpeed();
    }
      else if(user_input =='-')  {
        motor.decreaseSpeed();
      }
      else if( user_input == 'r' && seek_state == SEEK_QUIET)  {
        chooseNewDestination();
      }
      else if(user_input == 's' ) {
        if(master_state == MASTER_WAIT4STANDBY) {
          chooseNewDestination();
          master_state = MASTER_SEEK;
        }
      }
      else {
        //Serial.println("Invalid option entered.");
      } 
  }
}

/************************************************************************************
 * STATE MACHINE UTILITY FUNCTIONS
 ***********************************************************************************/
void chooseNewDestination()
{
  pot = analogRead(POT);
  
  destination = getTarget();
  int distance = destination - pot;

  if(distance > 0)
  {
    //Serial.println("going Forward");
    motor.forward();
    seek_state = SEEK_FORWARD;
  }
  else if(distance < 0)
  {
    //Serial.println("going reverse");
    motor.reverse();
    seek_state = SEEK_REVERSE;
  }
  else
  {
    chooseNewDestination();
  }
}

// A function to always select a destination which is
// far away fron the current potentiometer position.
int getTarget()
{
  int t = random(STOP_LOW, STOP_HIGH);
  int d = t - pot;
  if( abs(d) > DISTANCE ) {
    return t;
  }
  else {
    return getTarget();
  }
}

bool isArrived()
{
  if(seek_state == SEEK_FORWARD ) { // sub-state machine (seeking)
    //Serial.println(pot);
    if(pot >= destination) {
      return true;
    }
  } 
  else if(seek_state == SEEK_REVERSE ) {
    //Serial.println(pot);
    if(pot <=destination) {
      return true;
    }   
  }
  return false;
}


/************************************************************************************
 * TOUCH UTILITY FUNCTIONS
 ***********************************************************************************/
bool readTouch() {
  return touched;
}
