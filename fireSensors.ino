#include <SoftwareSerial.h>
#include <GyverEncoder.h>
#include <microLED.h>
#include <avr/eeprom.h>

#define FIRST_START     1     // ПРИ ПЕРВОЙ ЗАГРУЗКЕ ИЛИ СМЕНЕ КОЛ-ВА ДАТЧИКОВ ПОСТАВИТЬ 1, ПОСЛЕ ЭТОГО ПЕРЕПРОШИТЬ НА 0

#define RELAY_12V_PIN   10    // Пин реле
#define SIREN_PIN       11    // Пин сирены
#define RGB_PIN         12    // Пин RGB

#define ENC_S1_PIN      13    // Пин энкодера S1
#define ENC_S2_PIN      14    // Пин энкодера S2
#define ENC_SW_PIN      15    // Пин энкодера Key

#define BTN1_PIN        16    // Пин кнопки 1 (выключить сирену)
#define BTN2_PIN        17    // Пин кнопки 2 (общий сброс)

#define SIM_RX_PIN      18    // Пин RX ардуино
#define SIM_TX_PIN      19    // Пин TX ардуино

#define COUNT_SENSORS   16    // Количество датчиков
#define FIRE_VALUE      100   // Пороговое значение при пожаре
#define TIME_RELOAD     3500  // Время перезагрузки
#define TIME_WAITFIRE   6500  // Время ожидания нового срабатывания после перезагрузки
#define TIME_FIRE       (TIME_RELOAD + TIME_WAITFIRE) // Время ожидания нового срабатывания после перезагрузки (включая время перезагрузки)
#define TIME_BREAK      1000  // Время ожидания сигнала о обрыве

enum statesSensors {
  SENSOR_OK,
  SENSOR_WAITBREAK,
  SENSOR_BREAK,
  SENSOR_WAITFIRE,
  SENSOR_FIRE
};

//#define SENSOR_OK         0     // Статус ОК
//#define SENSOR_WAITBREAK  1     // Статус Обрыв (Проверка)
//#define SENSOR_BREAK      2     // Статус Обрыв
//#define SENSOR_WAITFIRE   3     // Статус Пожар (проверка)
//#define SENSOR_FIRE       4     // Статус Пожар!

#define TIME_CHANGE_MODE  1000  // Время смены режима в меню

#define PHONE_NUMBER      "+79997051047"  // Номер с которым будет взаимодействие

void(* resetFunc) (void) = 0;   // функция "программного ресета"


class FireSensor {
  private:
    byte pin;
    statesSensors status = SENSOR_OK;
    bool state;
    long int fire_time = 0;
    long int break_time = 0;
    bool notify = false;

  public:
    FireSensor(byte _pin = 0, bool _state = true);
    byte getStatus();
    byte updateStatus();
    bool getState();
    void setState(bool _state);
    void setPin(byte _pin);
    void setStatus(byte _status);
    void setFireTime();
    long int getFireTime();
    void setBreakTime();
    long int getBreakTime();
    bool getNotify();
    void setNotify();
  
};

FireSensor::FireSensor(byte _pin, bool _state = true) {
  pin = _pin;
  state = _state;
}

byte FireSensor::getStatus() {
  return status;
}

byte FireSensor::updateStatus() {
  int val = analogRead(pin);
  byte _status = 0;
  
  if (val == 0) {
    _status = SENSOR_WAITBREAK; // Обрыв
  } else if (val > FIRE_VALUE) {
    _status = SENSOR_WAITFIRE;  // Пожар
  } else {
    _status = SENSOR_OK;        // ОК
  }

  return _status;
}

bool FireSensor::getState() {
  return state;
}

void FireSensor::setState(bool _state) {
  state = _state;
}

void FireSensor::setPin(byte _pin) {
  pin = _pin;
}

void FireSensor::setStatus(byte _status) {
  status = _status;
}

void FireSensor::setFireTime() {
  fire_time = millis();
}

long int FireSensor::getFireTime() {
  return fire_time;
}

void FireSensor::setBreakTime() {
  break_time = millis();
}

long int FireSensor::getBreakTime() {
  return break_time;
}

bool FireSensor::getNotify() {
  return notify;
}

void FireSensor::setNotify() {
  notify = true;
}



bool reloadSensors = false;             // перезагрузка?
long int timeStartReloading = 0;        // время запуска перезагрузки
bool stateSiren = false;                // состояние сирены

bool dataStates[COUNT_SENSORS];         // массив состояний (вкл/выкл)
FireSensor fireSensors[COUNT_SENSORS];  // массив датчиков
bool menuOpened = false;                // состояние меню (открыто/закрыто)
byte menuPos = 0;                       // позиция курсора меню
bool menuMode = false;                  // режим меню
long int menuTimeMode = 0;              // время смены режима

microLED< COUNT_SENSORS, RGB_PIN, -1, LED_WS2812, ORDER_GRB> strip;   // RGB лента
Encoder enc(ENC_S1_PIN, ENC_S2_PIN, ENC_SW_PIN);    // Энкодер

SoftwareSerial simSerial(SIM_RX_PIN, SIM_TX_PIN); // RX, TX

void setup() {
  strip.clear();    // выключить RGB диоды
  
  pinMode(RELAY_12V_PIN, OUTPUT);   // Инициализация реле
  pinMode(SIREN_PIN, OUTPUT);       // Инициализация сирены
  pinMode(BTN1_PIN, INPUT);         // Инициализация кнопки 1 (выкл сирены)
  pinMode(BTN2_PIN, INPUT);         // Инициализация кнопки 2 (сброс)

  simSerial.begin(9600);    //Скорость порта для связи Arduino с GSM модулем
  simSerial.println("AT");
  
  if (FIRST_START) {
    for (byte i = 0; i < COUNT_SENSORS; i++) {
      dataStates[i] = true;   // все датчики включены
    }
    
    // записываем по адресу 0, указав размер
    eeprom_write_block((void*)&dataStates, 0, sizeof(dataStates));
  } else {
    // читаем из адреса 0
    eeprom_read_block((void*)&dataStates, 0, sizeof(dataStates));
  }
  
  for (byte i = 0; i < COUNT_SENSORS; i++) {
    fireSensors[i].setPin(i);
    
    if (!dataStates[i]) {
      fireSensors[i].setState(false);
    }
  }
}

void loop() {
  if (!reloadSensors) {
  
    for (byte i = 0; i < COUNT_SENSORS; i++) {
      if (!fireSensors[i].getState()) continue;   // если датчик выключен, переходим к следующему
      
      byte newStatus = fireSensors[i].updateStatus();

      /* ПРОВЕРКА НА ПОЖАР */
      if ((newStatus == SENSOR_WAITFIRE) && (fireSensors[i].getStatus() != SENSOR_WAITFIRE)) {
        fireSensors[i].setStatus(newStatus);
        fireSensors[i].setFireTime();
        reloadSensors = true;
      } else if ((newStatus == SENSOR_WAITFIRE) && (fireSensors[i].getStatus() == SENSOR_WAITFIRE) && (abs(millis() - fireSensors[i].getFireTime()) < TIME_FIRE)) {
        // ПОВТОРНОЕ СРАБАТЫВАНИЕ
        fireSensors[i].setStatus(SENSOR_FIRE);
      } else if ((newStatus != SENSOR_WAITFIRE) && (fireSensors[i].getStatus() == SENSOR_WAITFIRE) && (abs(millis() - fireSensors[i].getFireTime()) > TIME_FIRE)) {
        // Время на повторное срабатывание вышло
        fireSensors[i].setStatus(newStatus);
      }

      /* ПРОВЕРКА НА ОБРЫВ */
      if ((newStatus == SENSOR_WAITBREAK) && (fireSensors[i].getStatus() != SENSOR_WAITBREAK)) {
        // запуск проверки на обрыв
        fireSensors[i].setStatus(newStatus);
        fireSensors[i].setBreakTime();
      } else if ((newStatus == SENSOR_WAITBREAK) && (fireSensors[i].getStatus() == SENSOR_WAITBREAK) && (abs(millis() - fireSensors[i].getBreakTime()) > TIME_BREAK)) {
        // Обрыв! Время вышло...
        fireSensors[i].setStatus(SENSOR_BREAK);
      } else if ((newStatus != SENSOR_WAITBREAK) && (fireSensors[i].getStatus() == SENSOR_WAITBREAK) && (abs(millis() - fireSensors[i].getBreakTime()) < TIME_BREAK)) {
        // Обрыва нет
        fireSensors[i].setStatus(newStatus);
      }

      /* ПРОВЕРКА НА ОК */
      if (newStatus == SENSOR_OK) {
        fireSensors[i].setStatus(newStatus);
      }
      
    }
    
  } else if (abs(millis() - timeStartReloading) > TIME_RELOAD) {
    reloadSensors = false;
    digitalWrite(RELAY_12V_PIN, LOW);
  }

  // если требуется перезагрузка датчиков - перезагружаем
  if ((reloadSensors) && (digitalRead(RELAY_12V_PIN) == LOW)) {
    digitalWrite(RELAY_12V_PIN, HIGH);
    timeStartReloading = millis();
  }

  // Проверка надо ли включить сирену
  for (byte i = 0; i < COUNT_SENSORS; i++) {
    if (fireSensors[i].getState() && fireSensors[i].getStatus() == SENSOR_FIRE) {
      if (!stateSiren) {
        stateSiren = true;
        digitalWrite(SIREN_PIN, HIGH);
        
        break;
      }
    }
  }

  // Надо ли позвонить
  for (byte i = 0; i < COUNT_SENSORS; i++) {
    if (fireSensors[i].getState() && fireSensors[i].getStatus() == SENSOR_FIRE && !fireSensors[i].getNotify()) {
      String call_command = strcat(strcat("ATD", PHONE_NUMBER), ";");
      simSerial.println(call_command);
      
      fireSensors[i].setNotify();
      
      break;
    }
  }

  enc.tick();

  if (enc.isClick() && !menuOpened) {
    menuOpened = true;        // меню открыто
    menuMode = false;         // режим при открытии
    menuTimeMode = millis();  // время открытия меню
    strip.clear();            // выключить RGB диоды
    enc.resetStates();        // сбросить состояния энкодера
  }

  if (menuOpened) {
    menu();
  }

  if (menuOpened) {
    // ВКЛЮЧЕНИЕ СВЕТОДИОДОВ РЕЖИМЕ РАБОТЫ - МЕНЮ
    if (abs(millis() - menuTimeMode) > TIME_CHANGE_MODE) {
      menuTimeMode = millis();
      menuMode = !menuMode;
      
      // RGB в меню
      for (byte i = 0; i < COUNT_SENSORS; i++) {
        if (fireSensors[i].getState() && !menuMode) {
          strip.leds[i] = mRGB(0, 255, 0);
        } else if (menuMode && i == menuPos) {    // если курсор на этом, подсветить белым
          strip.leds[i] = mRGB(255, 255, 255);
        } else {
          strip.leds[i] = mRGB(0, 0, 0);
        }
      }
      strip.clear();  // очистить
      strip.show();   // вывод изменений на ленту
      delay(1);       // для последующего вывода нужна задержка 40 мксек минимум
    }
  } else {
    // ВКЛЮЧЕНИЕ СВЕТОДИОДОВ В ОБЫЧНОМ РЕЖИМЕ РАБОТЫ
    for (byte i = 0; i < COUNT_SENSORS; i++) {
      if (fireSensors[i].getState()) {
        if (fireSensors[i].getStatus() == SENSOR_OK || fireSensors[i].getStatus() == SENSOR_WAITFIRE || fireSensors[i].getStatus() == SENSOR_WAITBREAK) {
          // ВКЛ ЗЕЛЕНЫЙ
          strip.leds[i] = mRGB(0, 255, 0);
        } else if (fireSensors[i].getStatus() == SENSOR_BREAK) {
          // ВКЛ ЖЕЛТЫЙ
          strip.leds[i] = mRGB(255, 255, 0);
        } else {
          // ВКЛ КРАСНЫЙ
          strip.leds[i] = mRGB(255, 0, 0);
        }
      } else {
        // ВЫКЛЮЧИТЬ
        strip.leds[i] = mRGB(0, 0, 0);
      }
    }
    strip.clear();  // очистить
    strip.show();   // вывод изменений на ленту
    delay(1);       // для последующего вывода нужна задержка 40 мксек минимум
  }


  // КНОПКИ
  if (digitalRead(BTN1_PIN) == HIGH) {
    digitalWrite(SIREN_PIN, LOW);
  }

  if (digitalRead(BTN2_PIN) == HIGH) {
    digitalWrite(RELAY_12V_PIN, LOW);
    digitalWrite(SIREN_PIN, LOW);
    strip.clear();  // очистить
    resetFunc();
  }

  // ЗВОНИТ ЛИ КТО?
  if (simSerial.available()) {
    //Serial.write(mySerial.read());
    String resp = "";              // Переменная для хранения результата
    resp = simSerial.readString();
    if (resp.lastIndexOf(PHONE_NUMBER) >= 0) {
      digitalWrite(SIREN_PIN, LOW);
      simSerial.println("ATH0");    // сбросить вызов
    }
  }
  //if (Serial.available())
  //  mySerial.write(Serial.read());
}

void menu() {
  if (enc.isTurn()) {
    if (enc.isRight() && (menuPos < COUNT_SENSORS - 1)) {
      menuPos++;
    }
    
    if (enc.isLeft() && (menuPos > 0)) {
      menuPos--;
    }
  }

  if (enc.isClick()) {
    dataStates[menuPos] = !dataStates[menuPos];
    fireSensors[menuPos].setState(dataStates[menuPos]);
  }

  if (enc.isHolded()) {
    eeprom_write_block((void*)&dataStates, 0, sizeof(dataStates));  // сохраняем изменения
    menuOpened = false;   // на удержание выход из меню
  }
}
