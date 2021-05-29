#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Adafruit_BMP280.h>
#include <Adafruit_SHT31.h>
#include <I2C_BM8563.h>
#include <WiFi.h>
#include <array>

LGFX gfx;

//
// RTC
//
constexpr int BM8563_I2C_SDA = 21;
constexpr int BM8563_I2C_SCL = 22;
I2C_BM8563 rtc(I2C_BM8563_DEFAULT_ADDRESS, Wire1);
using RTCDate = I2C_BM8563_DateTypeDef;
using RTCTime = I2C_BM8563_TimeTypeDef;

RTCDate DateInfo;
RTCTime TimeInfo;
bool onUpdateTime = true;

//
void updateDateTime()
{
  RTCTime newTimeInfo;
  rtc.getTime(&newTimeInfo);
  if (newTimeInfo.hours != TimeInfo.hours || newTimeInfo.minutes != TimeInfo.minutes)
  {
    onUpdateTime = true;
    TimeInfo = newTimeInfo;
    rtc.getDate(&DateInfo);
  }
}

//
void setupDateTime()
{
  const char *ssid = "ABCDEFG";
  const char *password = "******";
  const char *ntpServer = "ntp.jst.mfeed.ad.jp";

  WiFi.begin(ssid, password);
  Serial.printf("Wifi[%s] setup:", ssid);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("done.");

  // get time from NTP server(UTC+9)
  configTime(9 * 3600, 0, ntpServer);

  // Get local time
  struct tm timeInfo;
  if (getLocalTime(&timeInfo))
  {
    // Set RTC time
    RTCTime TimeStruct;
    TimeStruct.hours = timeInfo.tm_hour;
    TimeStruct.minutes = timeInfo.tm_min;
    TimeStruct.seconds = timeInfo.tm_sec;
    rtc.setTime(&TimeStruct);

    RTCDate DateStruct;
    DateStruct.weekDay = timeInfo.tm_wday;
    DateStruct.month = timeInfo.tm_mon + 1;
    DateStruct.date = timeInfo.tm_mday;
    DateStruct.year = timeInfo.tm_year + 1900;
    rtc.setDate(&DateStruct);
    Serial.println("time setting success");
  }

  //disconnect WiFi
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("time setting done.");
}

//
// ENT Unit II
//
Adafruit_SHT31 sht3x = Adafruit_SHT31(&Wire);
Adafruit_BMP280 bme = Adafruit_BMP280(&Wire);

float temp = 0.0f;
float humi = 0.0f;
float pres = 0.0f;

//
void updateEnv()
{
  pres = bme.readPressure() / 100.0f;
  temp = sht3x.readTemperature();
  humi = sht3x.readHumidity();
}

//
// battery
//

uint32_t getBatteryVoltage()
{
  return analogRead(35) * 25.1 / 5.1;
}

void drawBattery()
{
  // バッテリ
  gfx.drawRect(130, 38, 60, 20);
  auto bv = max(min(getBatteryVoltage(), (uint32_t)4200), (uint32_t)3600);
  int br = (int)((((float)bv - 3600.0f) / 600.0f) * 56.0f);
  gfx.fillRect(132, 40, br, 16, TFT_GREEN);
}

//
// button
//
struct Btn
{
  int pin;
  int state;
  int prev;

  Btn(int p)
  {
    pin = p;
    state = 1;
    prev = 1;
  }

  void update()
  {
    prev = state;
    state = digitalRead(pin);
  }

  bool press() const { return state == 0; }
  bool push() const { return prev != state ? !state : false; }
};
Btn BtnUp{37};
Btn BtnMid{38};
Btn BtnDown{39};

void btnUp()
{
  BtnUp.update();
  BtnMid.update();
  BtnDown.update();
}

//
// command
//
class Cmd
{
public:
  virtual ~Cmd() = default;
  virtual void execute() = 0;
  virtual const char *caption() const = 0;
};

//
class TimeSetting : public Cmd
{
public:
  ~TimeSetting() override = default;
  const char *caption() const { return "時刻合わせ"; }
  void execute() override
  {
    // Serial.println("時刻合わせ");
    setupDateTime();
  }
};

//
class NoOperation : public Cmd
{
public:
  ~NoOperation() override = default;
  const char *caption() const { return "--"; }
  void execute() override
  {
    Serial.println("何もしない");
  }
};

//
class SendData : public Cmd
{
public:
  ~SendData() override = default;
  const char *caption() const { return "データ送信"; }
  void execute() override
  {
    Serial.println("データ送信");
  }
};

//
TimeSetting cmdTimeSetting;
NoOperation cmdNoOperation;
SendData cmdSendData;

std::array<Cmd *, 3> commandList = {&cmdTimeSetting, &cmdNoOperation, &cmdSendData};
int selectCommand = commandList.size() / 2;
bool onUpdateCommand = false;

//
void updateCommand()
{
  if (BtnUp.push())
  {
    int next = selectCommand > 1 ? selectCommand - 1 : 0;
    if (next != selectCommand)
      onUpdateCommand = true;
    selectCommand = next;
  }
  else if (BtnDown.push())
  {
    int last = commandList.size() - 1;
    int next = selectCommand < last ? selectCommand + 1 : last;
    if (next != selectCommand)
      onUpdateCommand = true;
    selectCommand = next;
  }
  else if (BtnMid.push())
  {
    auto cmd = commandList[selectCommand];
    cmd->execute();
  }
}

//
const char *getCommandCaption()
{
  return commandList[selectCommand]->caption();
}

//
// application
//
void setup()
{
  Serial.begin(115200);
  Serial.println("Launch");
  gfx.init();
  gfx.clear();
  gfx.setFont(&fonts::lgfxJapanGothic_24);
  gfx.powerSaveOn();
  delay(100);

  // button
  pinMode(37, INPUT_PULLUP);
  pinMode(38, INPUT_PULLUP);
  pinMode(39, INPUT_PULLUP);

  // env unit
  if (!bme.begin(0x76))
  {
    Serial.println("Could not find a valid BMP280 sensor, check wiring!");
  }
  if (!sht3x.begin(0x44))
  {
    Serial.println("Could not find a valid SHT3X sensor, check wiring!");
  }
  delay(100);

  // rtc
  Wire1.begin(BM8563_I2C_SDA, BM8563_I2C_SCL);
  rtc.begin();
  Serial.println("setup done.");
  delay(100);
}

void loop()
{
  try
  {
    btnUp();
    updateCommand();
    updateEnv();
    updateDateTime();

    if (onUpdateTime || onUpdateCommand)
    {
      gfx.clear();
      gfx.startWrite();
      char buff[128];
      int y = 5;

      static const char *wd[7] = {"日", "月", "火", "水", "木", "金", "土"};
      snprintf(buff, sizeof(buff), "%04d/%02d/%02d(%s)",
               DateInfo.year, DateInfo.month, DateInfo.date, wd[DateInfo.weekDay]);
      gfx.drawString(buff, 5, y);
      y += 28;

      gfx.setTextSize(1.6f);
      snprintf(buff, sizeof(buff), "%02d:%02d", TimeInfo.hours, TimeInfo.minutes);
      gfx.drawString(buff, 24, y);
      gfx.setTextSize(1.0f);

      y += 42;
      auto print = [&](const char *fmt, float n)
      {
        snprintf(buff, sizeof(buff), fmt, n);
        gfx.drawString(buff, 5, y);
        y += 26;
      };
      print("気温:%0.1f℃", temp);
      print("湿度:%0.1f%%", humi);
      print("気圧:%0.1fhPa", pres);

      y += 10;
      snprintf(buff, sizeof(buff), "CMD:%s", getCommandCaption());
      gfx.drawString(buff, 5, y);

      drawBattery();
      gfx.endWrite();
    }
    onUpdateTime = false;
    onUpdateCommand = false;
  }
  catch (std::exception &e)
  {
    Serial.println(e.what());
    while (1)
      ;
  }

  delay(500);
}
