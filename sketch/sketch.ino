#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <NTPClient.h>
#include <WiFiUdp.h>


//OLED display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0X3C

//Input and output pins
#define BUZZER 18
#define LED_1 15
#define LED_2 2
#define CANCEL 34
#define OK 33
#define UP 35
#define DOWN 32
#define DHTPIN 12
#define LDR_L 36
#define LDR_R 39
#define SERVMO 19

//Parameters related to light intensity
float GAMMA = 0.7;
const float RL10 = 50;

//Variables for calculating angle of shaded glass
int Angle_Offset = 30;
float Controlling_Factor = 0.75;

//Parameters for store lighte intensity of left and right ldrs
float lux_R;
float lux_L;


//time initialization
#define NTP_SERVER     "pool.ntp.org"
int UTC_OFFSET = 5*60*60 + 30*60;;
#define UTC_OFFSET_DST 0
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//Wifi and mqtt clients
WiFiClient espClient;
PubSubClient mqttClient(espClient);


// to store light and temperature data 
char tempAr[10];
char lightAr1[10];
char lightAr2[10];

DHTesp dhtSensor;

int days = 0;
int hours = 0;
int minutes = 0;
int seconds = 0;

unsigned long timeNow = 0;
unsigned long timeLast = 0;

//Variables for schedule process
bool isScheduledON = false;
unsigned long scheduledOnTime;

bool alarm_enabled = true;
int n_alarms = 3;
int alarm_hours[] = {0, 1, 2};
int alarm_minutes[] = {1, 10, 25};
bool alarm_triggered[] = {false, false, false};

int n_notes = 8;
int C = 262;
int D = 294;
int E = 330;
int F = 349;
int G = 392;
int A = 440;
int B = 494;
int C_H = 523;
int notes[] = {C, D, E, F, G, A, B, C_H};

int current_mode = 0;
int max_modes = 5;
String modes[] = {"1 - Set Time", "2 - Set Alarm 1", "3 - Set Alarm 2", "4 - Set Alarm 3","5 - Disable Alarms"};

//servo moter initializing position
float pos = 0;
Servo servo;

void setup() {
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_1, OUTPUT);
  pinMode(CANCEL, INPUT);
  pinMode(OK, INPUT);
  pinMode(UP, INPUT);
  pinMode(DOWN, INPUT);
  pinMode(LDR_L, INPUT);
  pinMode(LDR_R, INPUT);

  dhtSensor.setup(DHTPIN, DHTesp::DHT22);
  servo.attach(SERVMO, 500, 2400);

  Serial.begin(115200);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  display.display();
  delay(2000);


  setupWifi();
  setupMqtt();

  timeClient.begin();
  timeClient.setTimeOffset(5.5*3600);
  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);

  display.clearDisplay();

  print_line("Welcome", 25, 0, 2);
  print_line("to", 50, 20, 2);
  print_line("MediBox!", 20, 40, 2);
  display.clearDisplay();
}

void loop() {
  //connect with mqtt broker
  if(!mqttClient.connected()){
    connectToBroker();
  }
  mqttClient.loop();

  //publish topics with data
  //publish left ldr data 
  mqttClient.publish("ADMIN-LIGHT-INTENSITY_L",lightAr1);
  //publish right ldr data
  mqttClient.publish("ADMIN-LIGHT-INTENSITY_R",lightAr2);
  //publish temperature data
  mqttClient.publish("ADMIN-TEMP",tempAr);
  
  update_time_with_check_alarm();
  if (digitalRead(OK) == LOW) {
    delay(200);
    go_to_menu();
  }
  //check temperature
  check_temp();

  //read ldr data
  read_ldr() ;

  //calculate the servo motor angle using data
  serv_mo();

  check_schedule();
}

//setup mqtt server
void setupMqtt() {
  mqttClient.setServer("test.mosquitto.org", 1883);
  mqttClient.setCallback(recieveCallback);
}

void recieveCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char payloadCharAr[length];
  for (int i = 0; i < length; i++){
    Serial.print((char)payload[i]);
    payloadCharAr[i] = (char)payload[i];
  }
  Serial.print("\n");
 
  //receive minimum angle 
  if (strcmp(topic, "ADMIN-ANGLE-OFFSET") == 0){
    Angle_Offset = atoi(payloadCharAr);
  }
  //receive control factor
  if (strcmp(topic, "ADMIN-CONTROLLING_FACTOR") == 0) {
    Controlling_Factor = atof(payloadCharAr);
  }
  //receive main switch status
  if (strcmp(topic, "ADMIN-MAIN-ON-OFF") == 0) {
    buzzerOn(payloadCharAr[0] == '1');
  }
  //receive scheduled time
  if(strcmp(topic, "ADMIN-SCH-ON") == 0){
    if(payloadCharAr[0] =='N'){
      isScheduledON = false;
    }else{
      isScheduledON = true;
      scheduledOnTime = atol(payloadCharAr);
    }
  }


}

void connectToBroker() {
  while(!mqttClient.connected()){
    Serial.println("Attempting MQTT connection");
    if(mqttClient.connect("ESP32-12345645454")){
      Serial.println("connected");
      mqttClient.subscribe("ADMIN-ANGLE-OFFSET");
      mqttClient.subscribe("ADMIN-CONTROLLING_FACTOR");
      mqttClient.subscribe("ADMIN-MAIN-ON-OFF");
      mqttClient.subscribe("ADMIN-SCH-ON");
    }else{
      Serial.println("failed");
      Serial.println(mqttClient.state());
      delay(1000);
    }
  }
}


void setupWifi() {
  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    display.clearDisplay();
    print_line("Connecting to WiFi", 0, 0, 2);
  }

  display.clearDisplay();
  print_line("Connected to WiFi", 0, 0, 2);
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  delay(500);
}

void buzzerOn(bool on){
  if(on){
    tone(BUZZER, C);
  }
  else{
    noTone(BUZZER);
  }
}

unsigned long getTime(){
  timeClient.update();
  return timeClient.getEpochTime();
}


//this function reads left and right ldr data and convert those values into 
//light intensities 
void read_ldr() {

//read the analoge values of ldrs
  int analogValue_L = analogRead(LDR_L);
  int analogValue_R = analogRead(LDR_R);

//calculate the left light intensity
  float voltage_L = analogValue_L / 1024.  * 5;
  float resistance_L = 2000 * voltage_L / (1 - voltage_L/5);
  lux_L = pow (RL10 * 1e3 * pow(10,GAMMA) / resistance_L, (1 / GAMMA))/13460;
  String(lux_L).toCharArray(lightAr1,10);
  Serial.println(lux_L);

//calculate the right light intensity
  float voltage_R = analogValue_R / 1024.  * 5;
  float resistance_R = 2000 * voltage_R / (1- voltage_R/5);
  lux_R = pow (RL10 * 1e3 * pow(10,GAMMA) / resistance_R, (1 / GAMMA))/13460;
  String(lux_R).toCharArray(lightAr2,10);
  Serial.println(lux_R);

}

//this function calculate the position of servo motor
void serv_mo(){
  //calculate the pos according to the max intensity of ldrs
  //Angle_Offset and Controlling_Factor is taken from dashboard slider
  if(lux_L>=lux_R){
    pos = min(Angle_Offset*1.5 + (180 - Angle_Offset) * lux_L * Controlling_Factor,180.0);
    servo.write(pos);
  }
  else{
    pos = min(Angle_Offset*0.5 + (180 - Angle_Offset) * lux_R * Controlling_Factor,180.0);
    servo.write(pos);
  }

}

void check_schedule() {
  // set the buzzer to tone when scheduled time comes
  if (isScheduledON){
    unsigned long currentTime = getTime();
    if (currentTime > scheduledOnTime) {
      buzzerOn(true);
      isScheduledON = false;
      mqttClient.publish("ADMIN-MAIN-ON-OFF-ESP", "1");
      mqttClient.publish("ADMIN-SCH-ESP-ON", "0");
      Serial.println("Scheduled ON");
      Serial.println(currentTime);
      Serial.println(scheduledOnTime);
    }
  }
}



void print_line(String text, int column, int row, int text_size) {

  display.setTextSize(text_size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(column, row);
  display.println(text);
  display.display();
}

void print_time_now(void) {
  display.clearDisplay();
  print_line(String(hours)+":"+String(minutes)+":"+String(seconds),0,0,2);
}

void update_time() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  char timeHour[3];
  strftime(timeHour, 3, "%H", &timeinfo);
  hours = atoi(timeHour);

  char timeMinute[3];
  strftime(timeMinute, 3, "%M", &timeinfo);
  minutes = atoi(timeMinute);

  char timeSecond[3];
  strftime(timeSecond, 3, "%S", &timeinfo);
  seconds = atoi(timeSecond);

  char timeDay[3];
  strftime(timeDay, 3, "%d", &timeinfo);
  days = atoi(timeDay);

}

void ring_alarm() {
  display.clearDisplay();
  print_line("MEDICINE   TIME!", 0, 0, 2);
  digitalWrite(LED_1, HIGH);

  bool break_happened = false;
  while (break_happened == false && digitalRead(CANCEL) == HIGH) {
    for (int i = 0; i < n_notes; i++) {
      if (digitalRead(CANCEL) == LOW) {
        delay(200);
        break_happened = true;
        break;
      }
      tone(BUZZER, notes[i]);
      delay(500);
      noTone(BUZZER);
      delay(2);
    }
  }

  digitalWrite(LED_1, LOW);
  display.clearDisplay();
}

void update_time_with_check_alarm() {
  update_time();
  print_time_now();

  if (alarm_enabled == true) {
    for (int i = 0; i < n_alarms; i++) {
      if (alarm_triggered[i] == false && alarm_hours[i] == hours && alarm_minutes[i] == minutes) {
        ring_alarm();
        alarm_triggered[i] = true;
      }
    }
  }
}

int wait_for_button_press() {
  while (true) {
    if (digitalRead(UP) == LOW) {
      delay(200);
      return UP;
    }
    else if (digitalRead(DOWN) == LOW) {
      delay(200);
      return DOWN;
    }
    else if (digitalRead(OK) == LOW) {
      delay(200);
      return OK;
    }
    else if (digitalRead(CANCEL) == LOW) {
      delay(200);
      return CANCEL;
    }
    update_time();
  }
}

void go_to_menu() {
  // display.clearDisplay();
  // print_line("MENU",0,0,2);
  // delay(1000);
  while (digitalRead(CANCEL) == HIGH) {
    display.clearDisplay();
    print_line(modes[current_mode], 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == UP) {
      delay(200);
      current_mode += 1;
      current_mode = current_mode % max_modes;
    }

    else if (pressed == DOWN) {
      delay(200);
      current_mode -= 1;
      if (current_mode < 0) {
        current_mode = max_modes - 1;
      }
    }
    else if (pressed == OK) {
      delay(200);
      Serial.println(current_mode);
      run_mode(current_mode);
    }
    else if (pressed == CANCEL) {
      delay(200);
      break;
    }
  }
}

void set_time() {
  int temp_hour = 5;

  while (true) {
    display.clearDisplay();
    print_line("Hour offset:" + String(temp_hour), 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == UP) {
      delay(200);
      temp_hour += 1;
      if (temp_hour > 14) {
        temp_hour = -12;
      }
    }

    else if (pressed == DOWN) {
      delay(200);
      temp_hour -= 1;
      if (temp_hour < -12) {
        temp_hour = 14;
      }
    }
    else if (pressed == OK) {
      delay(200);
      hours = temp_hour;
      break;
    }
    else if (pressed == CANCEL) {
      delay(200);
      break;
    }
  }

  int temp_minute = 30;

  while (true) {
    display.clearDisplay();
    print_line("Minute offset:" + String(temp_minute), 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == UP) {
      delay(200);
      temp_minute += 15;
      temp_minute = temp_minute % 60;
    }

    else if (pressed == DOWN) {
      delay(200);
      temp_minute -= 15;
      if (temp_minute < 0) {
        temp_minute = 45;
      }
    }
    else if (pressed == OK) {
      delay(200);
      minutes = temp_minute;
      if (temp_hour >= 0){
        UTC_OFFSET = temp_hour*60*60 + temp_minute*60;
      }
      else{
        UTC_OFFSET = -(-temp_hour*60*60 + temp_minute*60);
      }
      configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
      update_time();  
      break;
    }
    else if (pressed == CANCEL) {
      delay(200);
      break;
    }
  }

  display.clearDisplay();
  print_line("Time is  set", 0, 0, 2);
  delay(1000);

}

void set_alarm(int alarm) {
  int temp_hour = alarm_hours[alarm];

  while (true) {
    display.clearDisplay();
    print_line("Enter hour:" + String(temp_hour), 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == UP) {
      delay(200);
      temp_hour += 1;
      temp_hour = temp_hour % 24;
    }

    else if (pressed == DOWN) {
      delay(200);
      temp_hour -= 1;
      if (temp_hour < 0) {
        temp_hour = 23;
      }
    }
    else if (pressed == OK) {
      delay(200);
      alarm_hours[alarm] = temp_hour;
      break;
    }
    else if (pressed == CANCEL) {
      delay(200);
      break;
    }
  }

  int temp_minute = alarm_minutes[alarm];

  while (true) {
    display.clearDisplay();
    print_line("Enter minute:" + String(temp_minute), 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == UP) {
      delay(200);
      temp_minute += 1;
      temp_minute = temp_minute % 60;
    }

    else if (pressed == DOWN) {
      delay(200);
      temp_minute -= 1;
      if (temp_minute < 0) {
        temp_minute = 59;
      }
    }
    else if (pressed == OK) {
      delay(200);
      alarm_minutes[alarm] = temp_minute;
      break;
    }
    else if (pressed == CANCEL) {
      delay(200);
      break;
    }
  }

  display.clearDisplay();
  print_line("Alarm is  set", 0, 0, 2);
  delay(1000);

}

void run_mode(int mode) {
  if (mode == 0) {
    set_time();
  }

  else if (mode == 1 || mode == 2 || mode == 3) {
    set_alarm(mode - 1);
  }

  else if (mode == 4) {
    alarm_enabled = false;
    print_line("All alarms are disabled", 0, 0, 2);
    delay(1000);
  }
}

void check_temp() {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  if (data.temperature > 35) {
    display.clearDisplay();
    print_line("TEMP HIGH", 0, 40, 1);
  }
  else if (data.temperature < 25) {
    display.clearDisplay();
    print_line("TEMP LOW", 0, 40, 1);
  }

  if (data.humidity > 40) {
    display.clearDisplay();
    print_line("HUMIDITY HIGH", 0, 50, 1);
  }
  else if (data.humidity < 20) {
    display.clearDisplay();
    print_line("HUMIDITY LOW", 0, 50, 1);
  }

    String(data.temperature,2).toCharArray(tempAr, 10);
}
