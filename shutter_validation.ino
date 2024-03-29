/*
Executes continuous cycle testing of shutter. 

Vitals are continuously updated at some frequency loop.

Validation is continuously executed at some frequency loop.  Compares "should be" shutter state to "is" shutter state.
If successful, data is logged to terminal.
If failure is detected, program enters an infinite while loop that does nothing, which freezes what has been printed to terminal till that point.
*/

/* macro defines */
// pin assignments
#define SOLENOID_CTRL_PIN 7    // DigOut solenoid control pin
#define BEAMBREAK_PIN 4   // DigIn shutter beam break pin
#define CURRENT_PIN A0    // AnaIn current reading
#define TEMPERATURE_PIN A1    // AnaIn temperature reading

// timing readability defines
#define LONG_DELAY_IN_MINUTES 180
#define SHORT_DELAY_IN_MINUTES 1000
#define SHUTTER_WAIT_TIMEOUT_MS 20   // time needed for shutter to fully open/close, per datasheet
#define TOGGLE_COUNTER_THRESH 60    // number of quick toggles at a time
#define LONG_TOGGLE_TIME 125   // long time between toggle cycles
#define SHORT_TOGGLE_TIME 12   // toggle cycle period
#define VALIDATION_LOOP_PERIOD 100    // validation loop period
#define SAMPLE_LOOP_PERIOD 100   // sample loop period

// other constants
#define CURRENT_SENSOR_OFFSET 25.4

/* const defines */
// defines for thermistor
static const uint16_t u16_BETA_100 = 3988;
static const uint16_t u16_R_DIVIDER = 3266;  // measure with meter and replace with actual value
static const uint16_t u16_ANALOG_MAX = 1023;
static const float f_T_REF = 298.15;
static const uint16_t u16_R_REF = 10000;

/* global variables */
// flags
static bool b_SB_shutter_open = true;   // reflects what software thinks shutter state is   
static bool b_IS_shutter_open = true;  // reflects actual state of shutter according to beam break
static bool b_shutter_just_changed = false;    // use to slightly delay validation loop--wait for shutter to fully open/close
                                               // gets toggled when (1) shutter is toggled, (2) after delay timeout

// timer variables
static uint32_t u32_time_start = 0;
static uint32_t u32_shutter_toggle_timeout = 0;
static uint32_t u32_validation_timeout = 0;
static uint32_t u32_sampling_timeout = 0;

// data
static uint32_t u32_consecutive_successes = 0;
static float f_temp_F = 0;
static float f_current_mA = 0;

// state machine enum
typedef enum {
  Stage1, Stage2, Stage3, Stage4, Stage5
} TestState;
static TestState CurrentState;

/*========================================================== 
Initialize pins, set initial shutter state, start loops 
timers.
===========================================================*/
void setup() {
  
  /* configure I/O pins */
  
  // digital output for solenoid driver control
  pinMode(SOLENOID_CTRL_PIN, OUTPUT);
  
  // digital input for beam break shutter feedback
  // also enable internal pull up for sensor OC output
  pinMode(BEAMBREAK_PIN, INPUT);
  digitalWrite(BEAMBREAK_PIN, HIGH);
  
  // analog input for current sensor, temperature (0-5V range, 1023 values)
  analogReference(DEFAULT);
  
  // initialize serial communication:
  Serial.begin(9600);
  
  // define data column labels
  Serial.print("\nElapsed time (ms)\t Temperature (F)\t Current (mA)\t Successes\n");
    
  // initialize shutter to be open 
  v_open_shutter();
  
  // start 10Hz validation loop
  u32_validation_timeout = millis() + VALIDATION_LOOP_PERIOD;  
  
  // start Hz temperature/current sampling loop
  u32_sampling_timeout = millis() + SAMPLE_LOOP_PERIOD;
  
  // put state machine into initial state
  CurrentState = Stage1; 
}


/*========================================================== 
Step through cycling routine
===========================================================*/
void loop() {

  // if shutter just changed, don't validate shutter state until fully open/closed
  if (b_shutter_just_changed) {
    
    // after timeout, toggle boolean
    if (u32_shutter_toggle_timeout <= millis()) {
      
      b_shutter_just_changed = false;
    } 
  }
  
  // else, shutter has fully opened/closed
  else {
    
    // validate shutter state 
    if (u32_validation_timeout <= millis()) {
        
      // if unable to validate state, end the test
      if (!b_validate_shutter_state()) {
        
        Serial.print("TEST STOPPED: believed and actual shutter states do not agree with each other.");
        while(true);
      }
      
      else {
        
        // otherwise, reset the 10Hz sampling timer
        u32_validation_timeout += VALIDATION_LOOP_PERIOD;  
      }
    }
  }
  
  // sample at lower frequency 
  if (u32_sampling_timeout <= millis()) {
    
    // update temperature
    v_update_temp();
    
    // update current
    v_update_current();
    
    // log data
    v_log_data(b_SB_shutter_open, f_temp_F, f_current_mA);
    
    // reset timeout value
    u32_sampling_timeout += SAMPLE_LOOP_PERIOD;   
  }
    
  // call cycle test state machine
  v_shutter_state_machine();
}


/*========================================================== 
Compares actual (beam break signal) and believed shutter states.
===========================================================*/
bool b_validate_shutter_state(void) {
  
  // digital input is TRUE if signal is successfully received (shutter open)  
  // if match, increment consecutive successes
  if (b_SB_shutter_open == digitalRead(BEAMBREAK_PIN)) {
    
    return true;
  }
  
    return false; 
}

/*========================================================== 
Toggle state of shutter
===========================================================*/
void v_toggle(void) {
  
  // toggle shutter state and boolean flag
  if (b_SB_shutter_open) {
    
    digitalWrite(SOLENOID_CTRL_PIN, LOW);
    b_SB_shutter_open = false;
  }
  
  else {
    
    digitalWrite(SOLENOID_CTRL_PIN, HIGH);
    b_SB_shutter_open = true;
  }
  
  // refresh boolean
  b_shutter_just_changed = true;
  
  // start timer to wait for shutter to fully open/close
  u32_shutter_toggle_timeout = millis() + SHUTTER_WAIT_TIMEOUT_MS;
  
  // increment success counter
  u32_consecutive_successes++;
}

/*========================================================== 
Sample solenoid temperature
===========================================================*/
void v_update_temp(void) {
  
  // read analog voltage from analog in pin
  uint32_t u32_V_analog = analogRead(TEMPERATURE_PIN);
  
  // back calculate thermistor resistance
  uint32_t u32_r_thermistor = u16_R_DIVIDER * u32_V_analog / (u16_ANALOG_MAX - u32_V_analog);
  
  // calculate temperature from steinhart-hart simplified equation
  float f_t_kelvin = u16_BETA_100 * f_T_REF / (u16_BETA_100 + f_T_REF * log((float)u32_r_thermistor / u16_R_REF));
  f_temp_F = (f_t_kelvin - 273.15) * 9.0 / 5.0 + 32.0;
}

/*========================================================== 
Sample solenoid current 
===========================================================*/
void v_update_current(void) {
  
  // current conversion is
  // i = (Vout - 0.25V) / 800mV/A
  // 0.25V is the offset
  // 800mV/A = .0008V/mA is the sensitivity
  // 1/0.800 = 1.25  
  uint32_t u32_V_analog = analogRead(CURRENT_PIN);
  f_current_mA = ((float) u32_V_analog *5/u16_ANALOG_MAX - 0.25) * 1250 - CURRENT_SENSOR_OFFSET;
}

/*========================================================== 
Print results to serial port on a single line,
separated by tab
===========================================================*/
void v_log_data(bool shutter, float temp, float current) {
  
  Serial.print(millis());
  Serial.print("\t");
  Serial.print(temp);
  Serial.print("\t");
  Serial.print(current);  
  Serial.print("\t");
  Serial.print(u32_consecutive_successes);
  Serial.print("\n");
}


/*========================================================== 
Opens the shutter, changes appropriate booleans, and starts
the timeout to allow for shutter to fully open
===========================================================*/
void v_open_shutter(void) {
  
  // open shutter
  digitalWrite(SOLENOID_CTRL_PIN, HIGH);
  b_SB_shutter_open = true;
  b_shutter_just_changed = true;
  
  // start timer to wait for shutter to fully open/close
  u32_shutter_toggle_timeout = millis() + SHUTTER_WAIT_TIMEOUT_MS;
  
  // increment success counter
  u32_consecutive_successes++;
}



/*========================================================== 
State machine for shutter testing routine
===========================================================*/
void v_shutter_state_machine(void) {
  
  static uint32_t u32_long_toggle_timer;
  static uint32_t u32_short_toggle_timer;
  static uint8_t u8_toggle_counter = 0;
  
  switch (CurrentState) 
  {
    
    case Stage1:
    {
      // start long timer
      u32_long_toggle_timer = millis() + LONG_TOGGLE_TIME;      
      // hold shutter open, if not already
      v_open_shutter();     
      
      // choose the next state
      CurrentState = Stage2;
    }
    break;
    
    case Stage2:
    {
      // toggle shutter after long timer expires
      if (u32_long_toggle_timer <= millis()) {
        
        // toggle the shutter
        v_toggle();
        
        // start short timer
        u32_short_toggle_timer = millis() + SHORT_TOGGLE_TIME;
        
        // increment toggle counter
        u8_toggle_counter++;
        
        // choose the next state
        CurrentState = Stage3;        
      }      
    }
    break;
    
    case Stage3:
    {
      // toggle shutter when short timer expires
      if (u32_short_toggle_timer <= millis()) {
        
        v_toggle();
        u8_toggle_counter++;
        
        // check if we've toggled the shutter enough
        if (u8_toggle_counter > TOGGLE_COUNTER_THRESH) {
          
          // reset toggle counter
          u8_toggle_counter = 0;
                    
          // restart test routing (after a short delay)
          CurrentState = Stage4;
        }
        
        // start short timer
        u32_short_toggle_timer = millis() + SHORT_TOGGLE_TIME;
      }
    }
    break;
    
    case Stage4:
    {
      // restart test routing after short delay
      if (u32_short_toggle_timer <= millis()) {
        
        CurrentState = Stage1;
      }
    }
    break;
  } 
}
