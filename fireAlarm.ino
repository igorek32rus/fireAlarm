#include <SoftwareSerial.h>
#include <GyverEncoder.h>
#include <microLED.h>
#include <avr/eeprom.h>

#define FIRST_START     0     // ПРИ ПЕРВОЙ ЗАГРУЗКЕ ИЛИ СМЕНЕ КОЛ-ВА ДАТЧИКОВ ПОСТАВИТЬ 1, ПОСЛЕ ЭТОГО ПЕРЕПРОШИТЬ НА 0

#define RELAY_12V_PIN   10    // Пин реле
#define SIREN_PIN       11    // Пин сирены
#define RGB_PIN         2     // Пин RGB

#define ENC_S1_PIN      30    // Пин энкодера S1
#define ENC_S2_PIN      28    // Пин энкодера S2
#define ENC_SW_PIN      26    // Пин энкодера Key

#define BTN1_PIN        3     // Пин кнопки 1 (выключить сирену)
#define BTN2_PIN        4     // Пин кнопки 2 (общий сброс)

#define SIM_RX_PIN      18    // Пин RX ардуино
#define SIM_TX_PIN      19    // Пин TX ардуино

#define COUNT_SENSORS   16    // Количество датчиков
#define FIRE_VALUE      100   // Пороговое значение при пожаре
#define SHORT_CIRCUIT   1000  // Значение от которого будет считаться КЗ
#define TIME_RELOAD     3500  // Время перезагрузки
#define TIME_WAITFIRE   6500  // Время ожидания нового срабатывания после перезагрузки
#define TIME_FIRE       (TIME_RELOAD + TIME_WAITFIRE) // Время ожидания нового срабатывания после перезагрузки (включая время перезагрузки)
#define TIME_BREAK      1000  // Время ожидания сигнала об обрыве

#define TIME_CHANGE_MODE  250  // Время смены режима в меню (мигание)
#define PHONE_NUMBER      "+79997051047"  // Номер с которым будет взаимодействие

#define SENSOR_OK             0     // Статус ОК
#define SENSOR_WAITBREAK      1     // Статус Обрыв (Проверка)
#define SENSOR_BREAK          2     // Статус Обрыв
#define SENSOR_WAITFIRE       3     // Статус Пожар (проверка)
#define SENSOR_FIRE           4     // Статус Пожар!
#define SENSOR_SHORT_CIRCUIT  5     // Статус КЗ


class FireSensor {
  private:
    uint8_t pin;
    byte status = SENSOR_OK;
    bool state;
    long int fire_time = 0;
    long int break_time = 0;
    bool notify = false;

  public:
    FireSensor(uint8_t _pin = 0, bool _state = true);
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

FireSensor::FireSensor(uint8_t _pin, bool _state = true) {
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
  } else if ((val > FIRE_VALUE) && (val < SHORT_CIRCUIT)) {
    _status = SENSOR_WAITFIRE;  // Пожар
  } else if (val >= SHORT_CIRCUIT) {
    _status = SENSOR_SHORT_CIRCUIT;   // КЗ
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

void FireSensor::setPin(uint8_t _pin) {
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


void(* resetFunc) (void) = 0;           // функция "программного ресета"

bool reloadSensors = false;             // перезагрузка датчиков?
unsigned long timeStartReloading = 0;   // время запуска перезагрузки датчиков
bool resetSystem = false;               // перезагрузка системы?
unsigned long timeStartReset = 0;       // время запуска перезагрузки системы
bool stateSiren = false;                // состояние сирены

bool dataStates[COUNT_SENSORS];         // массив состояний (вкл/выкл)
FireSensor fireSensors[COUNT_SENSORS];  // массив датчиков
bool menuOpened = false;                // состояние меню (открыто/закрыто)
byte menuPos = 0;                       // позиция курсора меню
bool menuMode = false;                  // режим меню
long int menuTimeMode = 0;              // время смены режима

microLED<COUNT_SENSORS, RGB_PIN, MLED_NO_CLOCK, LED_WS2818, ORDER_GRB, CLI_AVER> strip;   // RGB лента
Encoder enc(ENC_S1_PIN, ENC_S2_PIN, ENC_SW_PIN);    // Энкодер

SoftwareSerial simSerial(SIM_RX_PIN, SIM_TX_PIN);   // RX, TX

void setup() {
  strip.clear();              // выключить RGB
  strip.setBrightness(25);    // яркость RGB (0-255)

  pinMode(RELAY_12V_PIN, OUTPUT);   // Инициализация реле
  pinMode(SIREN_PIN, OUTPUT);       // Инициализация сирены
  pinMode(BTN1_PIN, INPUT);         // Инициализация кнопки 1 (выкл сирены)
  pinMode(BTN2_PIN, INPUT);         // Инициализация кнопки 2 (сброс)

  pinMode(BTN1_PIN, HIGH);    // Включаем подтягивающий к 1 кнопке
  pinMode(BTN2_PIN, HIGH);    // Включаем подтягивающий ко 2 кнопке

  enc.setType(TYPE2);       // тип энкодера

  simSerial.begin(9600);    // Скорость порта для связи Arduino с GSM модулем
  simSerial.println("AT");  // проверочная команда на SIM

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
    fireSensors[i].setPin(i + 54);            // A0 - это 54 пин Arduino Mega. Добавляем все поочередно
    fireSensors[i].setState(dataStates[i]);
  }

}

void loop() {
  if (!reloadSensors) {   // если не выполняется перезагрузка

    // читаем все датчики
    for (byte i = 0; i < COUNT_SENSORS; i++) {
      if (!fireSensors[i].getState()) continue;   // если датчик выключен, переходим к следующему

      byte newStatus = fireSensors[i].updateStatus();

      /* ПРОВЕРКА НА ПОЖАР */
      if ((newStatus == SENSOR_WAITFIRE) && (fireSensors[i].getStatus() != SENSOR_WAITFIRE) && (fireSensors[i].getStatus() != SENSOR_FIRE)) {
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

      /* ПРОВЕРКА НА КЗ */
      if (newStatus == SENSOR_SHORT_CIRCUIT) {
        fireSensors[i].setStatus(newStatus);
      }

      /* ПРОВЕРКА НА ОК */
      if (newStatus == SENSOR_OK) {
        fireSensors[i].setStatus(newStatus);
      }

    }

  } else if (abs(millis() - timeStartReloading) > TIME_RELOAD) {  // если перезагрузка датчиков выполнилась
    reloadSensors = false;
    digitalWrite(RELAY_12V_PIN, LOW);   // переводим состояние реле в постоянно вкл
  }

  // если требуется перезагрузка датчиков - перезагружаем
  if ((reloadSensors) && (digitalRead(RELAY_12V_PIN) == LOW)) {
    digitalWrite(RELAY_12V_PIN, HIGH);
    timeStartReloading = millis();
  }

  // Проверка надо ли включить сирену
  for (byte i = 0; i < COUNT_SENSORS; i++) {
    if (fireSensors[i].getState() && fireSensors[i].getStatus() == SENSOR_FIRE) {
      if (!stateSiren) {  // если сирена до этого не включалась, включаем
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

  bool clicked = enc.isClick();   // читаем состояние энкодера, был ли клик

  if (clicked && !menuOpened) {   // если был клик энкодера и меню небыло открыто
    menuOpened = true;        // меню открыто
    menuMode = false;         // режим при открытии
    menuTimeMode = millis();  // время открытия меню
    strip.clear();            // выключить RGB диоды
    enc.resetStates();        // сбросить состояния энкодера
    clicked = false;          // сбрасываем клик, чтобы небыло вызова в меню
  }

  if (menuOpened) {
    menu(clicked);    // вызываем меню с сотоянием клика энкодера
  }

  if (menuOpened) {
    // ВКЛЮЧЕНИЕ СВЕТОДИОДОВ РЕЖИМЕ РАБОТЫ - МЕНЮ
    if (abs(millis() - menuTimeMode) > TIME_CHANGE_MODE) {
      menuTimeMode = millis();
      menuMode = !menuMode;

      // RGB в меню
      for (byte i = 0; i < COUNT_SENSORS; i++) {
        if (fireSensors[i].getState()) {
          strip.leds[i] = mRGB(0, 255, 0);
        } else {
          strip.leds[i] = mRGB(0, 0, 0);
        }

        if (menuMode && i == menuPos) {    // если курсор на этом, подсветить белым
          strip.leds[i] = mRGB(255, 255, 255);
        }
      }

      strip.show();   // вывод изменений на ленту
      unsigned long timeWait = micros();
      while (abs(micros() - timeWait) < 40) {}  // для последующего вывода нужна задержка 40 мксек минимум
      // delay(1);       // для последующего вывода нужна задержка 40 мксек минимум
    }
  }

  if (!menuOpened) {
    // ВКЛЮЧЕНИЕ СВЕТОДИОДОВ В ОБЫЧНОМ РЕЖИМЕ РАБОТЫ
    for (byte i = 0; i < COUNT_SENSORS; i++) {
      if (!fireSensors[i].getState()) {
        // ВЫКЛЮЧИТЬ
        strip.leds[i] = mRGB(0, 0, 0);
        continue;
      }

      if (fireSensors[i].getStatus() == SENSOR_BREAK) {
        // ВКЛ ЖЕЛТЫЙ
        strip.leds[i] = mRGB(255, 255, 0);
      } else if (fireSensors[i].getStatus() == SENSOR_SHORT_CIRCUIT) {
        // ВКЛ СИНИЙ
        strip.leds[i] = mRGB(0, 0, 255);
      } else if (fireSensors[i].getStatus() == SENSOR_FIRE) {
        // ВКЛ КРАСНЫЙ
        strip.leds[i] = mRGB(255, 0, 0);
      } else {
        // ВКЛ ЗЕЛЕНЫЙ
        strip.leds[i] = mRGB(0, 255, 0);
      }
    }

    strip.show();   // вывод изменений на ленту
    unsigned long timeWait = micros();
    while (abs(micros() - timeWait) < 40) {}  // для последующего вывода нужна задержка 40 мксек минимум
    // delay(1);
  }


  // КНОПКИ
  if (digitalRead(BTN1_PIN) == HIGH) {    // Выкл сирену
    digitalWrite(SIREN_PIN, LOW);
  }

  if (digitalRead(BTN2_PIN) == HIGH) {    // Запустить сброс
    resetSystem = true;
    timeStartReset = millis();

    digitalWrite(RELAY_12V_PIN, HIGH);    // Выключить датчики
    digitalWrite(SIREN_PIN, LOW);         // Выключить сирену
    strip.clear();  // очистить RGB
  }

  // если был запущен сброс системы, спустя TIME_RELOAD - перезагрузка программы
  if (resetSystem && abs(millis() - timeStartReset) > TIME_RELOAD) {
    digitalWrite(RELAY_12V_PIN, LOW);
    resetFunc();
  }

  // Ответ от SIM500l
  if (simSerial.available()) {
    //Serial.write(mySerial.read());
    String resp = "";              // Переменная для хранения ответа
    resp = simSerial.readString();

    // если в выводе значится номер телефона, вырубаем сирену
    if (resp.lastIndexOf(PHONE_NUMBER) >= 0) {
      digitalWrite(SIREN_PIN, LOW);
      simSerial.println("ATH0");    // сбросить вызов
    }
  }

  // if (Serial.available())
  //   mySerial.write(Serial.read());
}

void menu(bool clicked) {
  if (clicked) {
    dataStates[menuPos] = !dataStates[menuPos];
    fireSensors[menuPos].setState(dataStates[menuPos]);
  }

  if (enc.isTurn()) {
    if (enc.isRight() && (menuPos < COUNT_SENSORS - 1)) {
      menuPos++;
    }

    if (enc.isLeft() && (menuPos > 0)) {
      menuPos--;
    }
  }

  if (enc.isHolded()) {
    eeprom_write_block((void*)&dataStates, 0, sizeof(dataStates));  // сохраняем изменения
    menuOpened = false;   // на удержание выход из меню
  }

}