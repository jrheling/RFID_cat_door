// (c) 2016 Joshua Heling <jrh@netfluvia.org>
// BSD licensed

#include <SoftwareSerial.h>
#include <Servo.h>
#include <ThreeColorLED.h>

// ugh
//#define LOG_DEBUG 
#define LOG_WARN

int val;
const int ledPin = 13;
const int RF_RECEIVE_pin=2; 
const int RF_TRANSMIT_pin=4; 
const int button_pin=3;
const int SPKR_pin = 7;
const int servo_pin = 8;
const int redLED = 11;
const int greenLED = 12;

Servo servo;
SoftwareSerial mySerial(RF_RECEIVE_pin, RF_TRANSMIT_pin);

ThreeColorLED tcl(greenLED, redLED);

int incomingByte = 0; 
int led_state=0;
bool doorOpen = true;
unsigned long openTime = 0;
const unsigned long openDuration = 10 * 1000; //10s

const unsigned long debounce_interval = 50;  // ms

volatile boolean b_pressed = false;  // set in ISR, based on action that triggered interrupt
volatile boolean b_changed = false;
volatile unsigned long b_down_at = 0;
volatile unsigned long b_up_at = 0;
volatile unsigned long b_int_time = 0;
volatile unsigned long last_b_int_time = 0;
boolean button_down = false;         // represents logical state of the button (i.e. in the state machine)

unsigned long last_beep = 0;    // used for beeping once / sec. while button is down

#define MODE_REGULAR 1
#define MODE_OPEN 2
#define MODE_LOCK 3
#define MODE_ENROLL 4
#define MODE_TRAINING 5
#define MODE_SHOWAUTH 6
#define MODE_CLEAR 10

boolean waiting_for_confirmation = false;

unsigned long confirmation_wait_start_ts = 0;
const unsigned long confirmation_timeout = 10 * 1000; // 10s

unsigned short int proposed_mode = MODE_REGULAR;
unsigned short int current_mode = MODE_REGULAR;

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  Serial.println("RFID test!");
  pinMode(ledPin, OUTPUT);
  pinMode(SPKR_pin, OUTPUT);

  tcl.setColor(TCL_CLR_GREEN);

  servo.attach(servo_pin);
  servo.write(0);
  servo.detach();

  closeDoor();
  
  attachInterrupt(digitalPinToInterrupt(button_pin),buttonChangeISR,CHANGE);  

  // Set up interrupt to call tcl.update() 
  //
  // Timer0 is already used for millis() - we'll just interrupt somewhere
  // in the middle and call the "Compare A" function below
  //  (from https://learn.adafruit.com/multi-tasking-the-arduino-part-2/timers)
  OCR0A = 0xAF;
  TIMSK0 |= _BV(OCIE0A);
}

void openDoor() {
  if (doorOpen == false) {
    tcl.saveState();  // FIXME - confirm we need this -- is there a time when we come here with an arbitrary blink going on that needs to be persisted across the door open?
    tcl.set(TCL_CLR_GREEN, TCL_CLR_NONE, TCL_BLINK_SLOW);    
    Serial.println("opening door");
    servo.attach(servo_pin);
    servo.write(100);
    delay(250);
    servo.detach();  // not sure if this is needed each time (probably not)
    doorOpen = true;
  }
  // even if the door was already open, reset the timer
  openTime = millis();
}

void closeDoor() {
  if (doorOpen == true) {
    Serial.println("closing door"); 
    servo.attach(servo_pin);
    servo.write(0);
    delay(250);
    servo.detach();

    tcl.restoreState();
    tcl.setColor(TCL_CLR_YELLOW);
    
    doorOpen = false;
  }
}

// Interrupt is called once a millisecond
SIGNAL(TIMER0_COMPA_vect) {    
  tcl.update();
}

void buttonChangeISR() {  
  b_int_time = millis();
  if (b_int_time - last_b_int_time > debounce_interval) {
#ifdef LOG_DEBUG
    Serial.println("!! INT !!");
#endif
    if (digitalRead(button_pin) == LOW) {
      b_pressed = false;
    } else {
      b_pressed = true;
    }
    b_changed = true;
    last_b_int_time = b_int_time;
  }
}

// called from loop() to update button state when a change was
//   detected.  Relies on the globals set by the ISR, and also does
//   a second layer of debounce-like logic to handle cases where we
//   see a second change event of the same type (e.g. a button down 
//   following another button down with no up between them).  
//
//  returns: 0 if button was depressed
//           N if button was released, where N is the duration of button press in ms
unsigned long manageButtonState() {
  unsigned long press_duration = 0;
  
#ifdef LOG_DEBUG
  Serial.println("??? Handling a change:");
  Serial.print(".. b_pressed = ");
  Serial.println(b_pressed);
  Serial.print(".. button_down = ");
  Serial.println(button_down);
  Serial.print(".. current value = ");
  Serial.println(digitalRead(button_pin));
#endif
  
  if (button_down) {   // it's down now, so we expect to see it go up
    bool button_was_released = false;
    if (b_pressed && (digitalRead(button_pin) == HIGH)) {  // unexpected, based on current state
#ifdef LOG_WARN          
      Serial.println("***** unexpected redundant down");
      Serial.print("*****   - value now is: ");
      Serial.println(digitalRead(button_pin));
      Serial.println("*****   <pausing briefly and re-reading> ");
      delay(30);
#endif          
      if (digitalRead(button_pin) == LOW) { // back to normal
#ifdef LOG_WARN
        Serial.println("after pause found button up, as expected");
#endif
        button_was_released = true;
      }
    } else {  // button was released (went up)
      button_was_released = true;
    }

    if (button_was_released) {
      b_up_at = last_b_int_time;
      button_down = false;

#ifdef LOG_DEBUG
      Serial.print("[/] up at: ");
      Serial.println(b_up_at);
      Serial.print("    (had been down at ");  // debugging - sometimes the computed diff. is wrong
      Serial.print(b_down_at);
      Serial.println(")");
#endif  
      press_duration = b_up_at - b_down_at;
#ifdef LOG_DEBUG
      Serial.print("    press_duration = ");
      Serial.print(press_duration);
      Serial.println(" ms");
#endif     
      Serial.print(" - - button was down for ");
      Serial.print((float)(press_duration/1000.0));
      Serial.println(" seconds");          
    }
  } else {   // it's up now, so we expect to see it go down
    bool button_was_depressed = false;
    if (b_pressed == false) {
#ifdef LOG_WARN          
      Serial.println("*****unexpected redundant up");          
      Serial.print("*****   - value now is: ");
      Serial.println(digitalRead(button_pin));
      Serial.println("*****   <pausing briefly and re-reading> ");
      delay(30);          
#endif          
      if (digitalRead(button_pin) == HIGH) { // back to normal
#ifdef LOG_WARN
        Serial.println("after pause found button down, as expected");
#endif
        button_was_depressed = true;
      }
    } else {
      button_was_depressed = true;
    }

    if (button_was_depressed) {
      b_down_at = last_b_int_time;
      button_down = true;
      last_beep = millis();
#ifdef DEBUG          
      Serial.print("[\\] down at: ");
      Serial.println(b_down_at);
#endif          
    }
  }
  b_changed = false;    
  return(press_duration);
}  // end manageButtonState()

// make a buzzer (error) sound
void bzzt() {
  tone(7, 62, 500); // B1 for 0.5s
}

// happy bleep 
void confirmation_sound() {
  tone(7, 392, 300); //  G4
  delay(200);
  tone(7, 494,300);  // B4
  delay(200);
  tone(7, 523, 300); // C5
}

// takes: mode to change to (see README for description of modes)
// returns nothing
void change_mode(int newmode) {
  switch (newmode) {
    case MODE_REGULAR:
      Serial.print("Changing to mode 1");
      tcl.set(TCL_CLR_YELLOW, TCL_CLR_NONE, TCL_BLINK_NONE);
      current_mode = newmode;
      break;
    case MODE_OPEN:
      openDoor();
      Serial.print("Changing to mode 2");
      tcl.set(TCL_CLR_GREEN, TCL_CLR_NONE, TCL_BLINK_NONE);
      current_mode = newmode;
      break;      
    case MODE_LOCK:
      closeDoor();
      Serial.print("Changing to mode 3");
      tcl.set(TCL_CLR_RED, TCL_CLR_NONE, TCL_BLINK_NONE);
      current_mode = newmode;
      break;
    case MODE_ENROLL:
      Serial.print("Changing to mode 4"); 
      tcl.set(TCL_CLR_GREEN, TCL_CLR_RED, TCL_BLINK_NORM);
      current_mode = newmode;  // FIXME - probably don't want to actually do this, since this is just a one-shot command not a real mode
      break;    
    case MODE_TRAINING:
      Serial.print("Changing to mode 5");
      tcl.set(TCL_CLR_YELLOW, TCL_CLR_NONE, TCL_BLINK_NORM);
      current_mode = newmode;
      break;          
    case MODE_SHOWAUTH:
      // show currently-authorized fobs
      Serial.print("Changing to mode 6");      
      current_mode = newmode;  // FIXME - probably don't want to actually do this, since this is just a one-shot command not a real mode
      break;          
    case MODE_CLEAR:
      // clear authorized keys list
      Serial.print("Changing to mode 10");      
      current_mode = newmode;  // FIXME - probably don't want to actually do this, since this is just a one-shot command not a real mode
      break;          
    default:
#ifdef WARN
      Serial.print("Ignoring request to change to invalid mode ");
      Serial.println(newmode);    
      break; 
#endif
    break;
  }  
}

void loop() 
{
    unsigned long now = millis();
  
    if (b_changed) {                     // handle button change (state updated by ISR)
      unsigned long press_time = manageButtonState();
      if (press_time > 0) {        
        if (waiting_for_confirmation) {  // mode change pending confirmation
         // the press we just got confirmed the pending change
          confirmation_sound();
          waiting_for_confirmation = false;          
          change_mode(proposed_mode);
        } else {                         // new mode change request
          int s = press_time / 1000;      
          if (((s >= 1) && (s <= 5)) or s == 10) {   // was a valid mode requested?
            proposed_mode = s;
            waiting_for_confirmation = true;
            confirmation_wait_start_ts = millis();
            
            // beep out the count of the requested mode and flash quickly to 
            //   indicate we need confirmation     
            tcl.saveState();       
            tcl.set(TCL_CLR_RED, TCL_CLR_NONE, TCL_BLINK_FAST);

            for (int i = 1; i <= proposed_mode; i++) {
              delay(525);
              tone(7, 262, 400); // C4
            }            
          } else {            
            bzzt();             
          }
        }
      }      
    } else {                   // no button pressed
      if (waiting_for_confirmation) {
        if (now > (confirmation_wait_start_ts + confirmation_timeout)) {
          // timeout the proposed state change
          bzzt();
          tcl.restoreState();
          waiting_for_confirmation = false;
        }
      }
    }
    
    // beep once per second if the button is down
    //   - using value of 'now' from above - close enough
    if (button_down) {
      if (now - 1000 > last_beep) {
        tone(7, 65, 30);  // 10ms of C2
        last_beep = now;
      }
    }
    
    if (current_mode == MODE_REGULAR) {      
      // check to see if it's time to close the door
      if (doorOpen) {
        if ((now > openDuration) && (now - openDuration >= openTime)) {
          closeDoor();
        } 
      }
    
      if(RFID()==1)
      {
          digitalWrite(ledPin, HIGH);
          digitalWrite(SPKR_pin, HIGH);
          openDoor();
      }
        else
      {
          digitalWrite(ledPin, LOW);
          digitalWrite(SPKR_pin, LOW);
      }
    }
}


// function copied from ... (?)
int RFID() 
{
  byte i = 0;
  byte val = 0;
  byte code[6];
  byte checksum = 0;
  byte bytesread = 0;
  byte tempbyte = 0;
  int result = 0;

  if(mySerial.available()) {
    result=1;
    //Serial.println(mySerial.read());
    if((val = mySerial.read()) == 0x02) {// check for header 
      bytesread = 0; 
      Serial.println("I read it");
      while (bytesread < 12) {// read 10 digit code + 2 digit checksum
        if(mySerial.available()) { 
          val = mySerial.read();
          //Serial.println(val,HEX);
          if((val == 0x0D)||(val == 0x0A)||(val == 0x03)||(val == 0x02)) { // if header or stop bytes 
            break; // stop reading
          }

          // Do Ascii/Hex conversion:
          if ((val >= '0') && (val <= '9')) {
            val = val - '0';
          } else if ((val >= 'A') && (val <= 'F')) {
            val = 10 + val - 'A';
          }

          // Every two hex-digits, add byte to code:
          if (bytesread & 1 == 1) {
            // make some space for this hex-digit by
            // shifting the previous hex-digit with 4 bits to the left:
            code[bytesread >> 1] = (val | (tempbyte << 4));

            if (bytesread >> 1 != 5) {// If we're at the checksum byte,
              checksum ^= code[bytesread >> 1]; // Calculate the checksum... (XOR)
            };
          } else {
            tempbyte = val; // Store the first hex digit first...
          };
          bytesread++; // ready to read next digit
        } 
      } 

      // Output to Serial:

      if (bytesread == 12) { // if 12 digit read is complete
        Serial.print("5-byte code: ");
        for (i=0; i<5; i++) {
          if (code[i] < 16) mySerial.print("0");
          Serial.print(code[i], HEX);
          Serial.print(" ");
        }
        Serial.println();
        Serial.print("Checksum: ");
        Serial.print(code[5], HEX);
        Serial.println(code[5] == checksum ? " -- passed." : " -- error.");
        if(code[5] == checksum)
        result=1;
        Serial.println();
      }
      bytesread = 0;
    }
  }
  return result;
}

