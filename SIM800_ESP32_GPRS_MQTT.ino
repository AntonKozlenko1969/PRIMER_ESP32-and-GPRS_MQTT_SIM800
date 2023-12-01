
/* ====================================================================================================
 Демо версия взаимодествия ESP32 и SIM800L через очередь команд и параллельную основному циклу задачу
   ==================================================================================================== */

// Отладочная плата Lilygo T-CALL SIM800
// описание https://github.com/Xinyuan-LilyGO/LilyGo-T-Call-SIM800/tree/master

// #include <SoftwareSerial.h>                                   // Библиотека програмной реализации обмена по UART-протоколу
// SoftwareSerial SIM800(8, 9);                                  // RX, TX
// Определение пинов для взаимодействия с SIM800L
#define MODEM_RST             5
#define MODEM_PWRKEY          4
#define MODEM_POWER_ON       23
#define MODEM_TX             27
#define MODEM_RX             26

#define SIM800  Serial1
#define DIGIT_IN_PHONENAMBER 9  // количество цифр в номере

//#define NOSERIAL // Раскомментируйте это макроопределение, чтобы не использовать отладочный вывод в Serial

int relayPin[3] = {2, 25, 13};                                 // Пины с подключенными светодиодами
uint8_t btnLevel = 1;               // Уровни на цифровом входе при замыкании кнопки (младший бит) и признак фиксируемого выключателя (старший бит)

//String _response    = "";                                     // Переменная для хранения ответа модуля
// long lastUpdate = millis();                                   // Время последнего обновления
// long updatePeriod   = 60000;                                  // Проверять каждую минуту

String whiteListPhones = "069202891, 123456789, 987654321";   // Белый список телефонов
// Односимвольные константы
const char charCR = '\r';
const char* const strEmpty = "";
const char charQuote = '"';
const int8_t maxRelays = 3; // Количество каналов реле
const char charSlash = '/';

bool IsOpros = false; // признак однократной отправки Opros в модем
bool IsRestart = false; // признак однократной отправки Restart в модем
bool PIN_ready = false;
bool CALL_ready = false;
bool GET_GPRS_OK = false; // признак удачного HTTP GET запроса
bool GPRS_ready = false; // признак подключения GPRS
bool MQTT_connect = false;
bool TCP_ready=false;
bool modemOK = false; // общая готовность модема
bool comand_OK = false; // признак успешного выполнения текущей команды
bool SIM_fatal_error=false; //признак не вставленной СИМ карты или полного сбоя GSM модема
bool one_call = false; // признак однократного снятия трубки, т.к. при повторных сигналах вызова и сбое в модеме реле может срабатывать многократно 
TaskHandle_t Task3 = NULL; // Задача для ядра 0

// Названия топиков для MQTT
const char mqttRelayTopic[] PROGMEM = "/Relay";
const char mqttRelayConfirmTopic[] PROGMEM = "/Confirm"; //топик для публикаций роложения реле
const char mqttRelayConfigTopic[] PROGMEM = "/Config"; //топик для подписки и получения сообщений
//***** добавлено настройка LWT
  const char mqttDeviceStatusTopic[] PROGMEM = "/Status";
  const char mqttDeviceStatusOn[] PROGMEM = "online";
  const char mqttDeviceStatusOff[] PROGMEM = "offline";
  const char mqttDeviceIPTopic[] PROGMEM = "/LocalIP";
  const char MQTT_type[15] PROGMEM = "MQTT";  //"MQIsdp";  // тип протокола 
//Переменные для работы с SMS
// char SMS_incoming_num[DIGIT_IN_PHONENAMBER+7]; // номер с которого пришло СМС - для ответной СМС
// char SMS_text_num[DIGIT_IN_PHONENAMBER+1];  // номер телефона из СМС
// char SMS_text_comment[5+1]; // комментарий к номеру из СМС

int16_t alloc_num[3]={0,0,0}; //Количество имеющихся в телефонной книге номеров и общее возможное количество номеров
unsigned long t_rst = 0; //120*1000; // отследить интервал для перезапуска модема
int SMS_phoneBookIndex=0; // если номер уже есть в симке - его индекс, нет - ноль
int SMS_currentIndex = 0; // текущая СМС в обработке, если ноль ничего нет в обработке
bool IsComment=false;  //признак наличия прикрепленного к номеру комментария

unsigned long t_last_command = 0;  // последняя команда от модема для отслеживания ОК
int8_t flag_modem_resp = 0; // Признак сообщения полученного от модема (если необходимо обработать следующую строку с ОК)
                            // 1 - +CMGS: попытка отправить сообщение OK или ERROR
                            // 2 - +CPBF: попытка найти одиночный номер на симке
                            // 3 - +CPBW: попытка добавить / редактировать одиночный номер на симке
                            // 4 - +CPBW: завершено одиночное удаление номера из СМС - отправить ответ
                            // 5 - +CPBF: просмотр всех номеров из СМС - создание текстового файла
                            // 6 - > - запрос на ввод текста сообщения при его отправке после команды +CMGS
                            // 8 - > - запрос на ввод данных для их отправки на MQTT сервер

// Организация стека номеров команд и текста команд для отправки в модуль 
// для предотвращения одновременной отправки команды в модем при выполнении текущей команды
const int max_queue = 30;
const int max_text_com = 350;
 typedef struct{
     int com;     // номер команды 
     int com_flag;    // флаг команды, для отслеживания ее выполнения при обработке сообщения "OK" от модема SIM800
     char text_com[max_text_com]; // максимальная длина строки команд - 556 символов
   } mod_com;

 QueueHandle_t queue_comand; // очередь передачи команд в модуль SIM800 размер - int8_t [max_queue]
 QueueHandle_t queue_IN_SMS; // очередь обработки входящих СМС

String _mqttServer="gumoldova822.cloud.shiftr.io"; // MQTT-брокер
  uint16_t _mqttPort=1883; // Порт MQTT-брокера
  String _mqttUser="gumoldova822"; // Имя пользователя для авторизации
  String _mqttPassword="7iVIJbwz3VI8HaXd"; // Пароль для авторизации  
  String _mqttClient="ESP_Relay"; // Имя клиента для MQTT-брокера (используется при формировании имени топика для публикации в целях различия между несколькими клиентами с идентичным скетчем)


void setup() {
    #ifdef MODEM_RST
      // Keep reset high
      pinMode(MODEM_RST, OUTPUT);
      digitalWrite(MODEM_RST, HIGH);
    #endif

    pinMode(MODEM_PWRKEY, OUTPUT);
    pinMode(MODEM_POWER_ON, OUTPUT);

        // Set GSM module baud rate and UART pins
    SIM800.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);  // Скорость обмена данными с модемом

  Serial.begin(115200);                                         // Скорость обмена данными с компьютером
                                       
  Serial.println("Start!");

  for (int i = 0; i < 3; i++) {
    pinMode(relayPin[i], OUTPUT);                                 // Настраиваем пины в OUTPUT
    digitalWrite(relayPin[i], LOW); //HIGH
  }
  queue_comand = xQueueCreate(max_queue, sizeof(mod_com)); // очередь передачи команд в модуль SIM800 размер - int8_t [max_queue]
  queue_IN_SMS = xQueueCreate(max_queue, sizeof(int)); // очередь обработки СМС

  // Для демонстрации многозадачности
   // Настраиваем пины LED в OUTPUT
    pinMode(12, OUTPUT);                
    digitalWrite(12, LOW); 
    pinMode(14, OUTPUT);                
    digitalWrite(14, HIGH); 

  // Настраиваем пин кнопки в INPUT
    pinMode(36, (btnLevel & 0x01) != 0 ? INPUT : INPUT_PULLUP);   

// Создаем задачу с кодом из функции Task1code(),
  // с приоритетом 1 и выполняемую на ядре 0:
  xTaskCreatePinnedToCore(
                    GPRS_modem_traffic,   /* Функция задачи */
                    "Task3",     /* Название задачи */
                    2000,       /* Размер стека задачи */
                    NULL,        /* Параметр задачи */
                    1,           /* Приоритет задачи */
                    &Task3,      /* Идентификатор задачи, чтобы ее можно было отслеживать */
                    1);          /* Ядро для выполнения задачи (0) */

  vTaskDelay(30);
  // стартовые настройки модема
  add_in_queue_comand(6,"", 0);
  IsRestart = true; // признак однократной отправки Restart в модем
  add_in_queue_comand(9,"", 0); //удалить все смс сохраненные на СИМ карте

}

void loop() {
// Опросить модем раз в указанный интервал
  if (millis() - t_rst > 11*60*1000 && modemOK && !IsOpros && !SIM_fatal_error) 
 {    IsOpros = true;
      add_in_queue_comand (11,"", 0);         
 }

 // Если есть проблемы с модемом попытаться сбросить модем
 if (!modemOK && millis() - t_rst > 3*60*1000 && !IsRestart && !SIM_fatal_error) 
   {    IsRestart = true; // признак однократной отправки Restart в модем   
         add_in_queue_comand (6,"", 0);   
   }

  if ( _mqttServer != strEmpty && modemOK && !IsOpros && !SIM_fatal_error) {
      GPRS_MQTT_Reconnect();
   } 

if (SIM800.available())   {                   // Если модем, что-то отправил...
    String _response = "";              // Переменная для хранения ответа модуля
    // Получаем ответ от модема для анализа
    char inchar; int8_t inn_r=0; int8_t inn_n=0; uint inn_simv=0;
    _response=strEmpty;
    while (SIM800.available()){
        inchar = SIM800.read();
  //ответ от SIM800 заключен в "скобки" из двух символов <CR><CN> -- respons -- <CR><CN>
  //получиь, в каждом заходе, чистый ОДИНОЧНЫЙ ответ без этих "скобок"
        if (inchar == '\r') {++inn_r; ++inn_simv;}
        else if (inchar == '\n') { ++inn_n; ++inn_simv;
           if (inn_simv > 1 && inn_n == 2 && inn_r == 2) break;
          }
        else { _response += inchar; ++inn_simv;}
    }
      #ifndef NOSERIAL      
        Serial.println("          " + _response);     // Если нужно выводим в монитор порта  
        // for (int f=0;f<_response.length();++f){
        //   Serial.print(_response[f],HEX); Serial.print(' ');
        // }
      #endif 
    int firstIndex = 0;
    String textnumber = "";                    // переменая с текстовым значением номера из телеф. книги
    String textnumbercomment = "";            // переменая с текстовым значением коментария из телеф. книги  (не больше 6 символов)
    // ... здесь можно анализировать данные полученные от GSM-модуля
    if (flag_modem_resp == 8)  {
        if (_response.indexOf(F("CONNECT OK")) > -1) TCP_ready=true;
        if (_response.indexOf(F("CONNECT FAIL")) > -1) { MQTT_connect = false; TCP_ready=false;}      
    }  
    if ( _response.indexOf('>') > -1 && (flag_modem_resp == 6 || flag_modem_resp == 8)) // запрос от модема на ввод текста сообщения
       comand_OK = true; 
    else if (_response.indexOf(F("+CPIN: READY")) > -1) PIN_ready = true;
    else if (_response.indexOf(F("+CPIN: NOT READY")) > -1) {
      PIN_ready = false; MQTT_connect = false; TCP_ready=false; CALL_ready = false; modemOK = false;}
    else if (_response.indexOf(F("+CCALR: 1")) > -1) CALL_ready = true;
    else if (_response.indexOf(F("+CCALR: 0")) > -1) CALL_ready = false;
    else if (_response.indexOf(F("+CLIP:")) > -1) { // Есть входящий вызов  +CLIP: "069123456",129,"",0,"069123456asdmm",0  

      //one_call - признак однократного снятия трубки, т.к. при повторных сигналах вызова и сбое в модеме реле может срабатывать многократно 
      // и очередь команд забьется одинаковыми командами
        if (one_call) return; 
           one_call = true;

      int phoneindex = _response.indexOf(F("+CLIP: \""));// Есть ли информация об определении номера, если да, то phoneindex>-1
      String innerPhone = "";                   // Переменная для хранения определенного номера
      if (phoneindex >= 0) {                    // Если информация была найдена
        phoneindex += DIGIT_IN_PHONENAMBER-1;  // Парсим строку и ...
       // innerPhone = _response.substring(_response.indexOf("\"", phoneindex)-DIGIT_IN_PHONENAMBER, _response.indexOf("\"", phoneindex)); //innerPhone = _response.substring(phoneindex, _response.indexOf("\"", phoneindex)); // ...получаем номер
        innerPhone = _response.substring(_response.indexOf(charQuote, phoneindex)-DIGIT_IN_PHONENAMBER, _response.indexOf(charQuote, phoneindex)); //NEW // ...получаем номер
      #ifndef NOSERIAL          
        Serial.print("Number: "); Serial.println(innerPhone); // Выводим номер в монитор порта    
      #endif  
        //поиск текстового поля в ответе +CLIP: "069071234",129,"",0,"",0
        int last_comma_index = _response.lastIndexOf(',');
        int fist_comma_index = String(_response.substring(0,last_comma_index-1)).lastIndexOf(',');
        if ((last_comma_index-fist_comma_index) >= DIGIT_IN_PHONENAMBER)
          textnumber=_response.substring(fist_comma_index+2, fist_comma_index+2+DIGIT_IN_PHONENAMBER); 
        else textnumber="";

        if (_response.length() > fist_comma_index+2+DIGIT_IN_PHONENAMBER) //если в текстовом поле еще есть коментарий
        { 
          textnumbercomment=_response.substring(fist_comma_index+2+DIGIT_IN_PHONENAMBER, _response.length()-3);
         #ifndef NOSERIAL            
          Serial.print("TextNumberComment: "); Serial.println(textnumbercomment);
         #endif 
        }
        #ifndef NOSERIAL  
          Serial.print("TextNumber: "); Serial.println(textnumber);
        #endif  
      }
      // Проверяем, чтобы длина номера была больше 6 цифр, и номер должен быть в списке
      // whiteListPhones Белый список телефонов максимум 3 номера по 8 симолов
      if (innerPhone.length() > DIGIT_IN_PHONENAMBER-3 && whiteListPhones.indexOf(innerPhone) > -1) {
         regular_call(); // Если звонок от БЕЛОГО номера из EEPROM - ответить, включить реле и сбросить вызов
        #ifndef NOSERIAL  
          Serial.println("Call from WhiteList");
        #endif  
      }         
      else if (innerPhone == textnumber && textnumber.length() == DIGIT_IN_PHONENAMBER){
        regular_call(); // Если звонок от БЕЛОГО номера из СИМ карты - ответить, включить реле и сбросить вызов  
        #ifndef NOSERIAL  
          Serial.println("Call from SIM number");
        #endif  
      }  

    // Если нет, то отклоняем вызов  
      else   add_in_queue_comand(30, "H", 0);
    }
    //********* проверка отправки SMS ***********
    else if (_response.indexOf(F("+CMGS:")) > -1) {       // Пришло сообщение об отправке SMS
      //flag_modem_resp = 1; //Выставляем флаг и далее при получении ответа от модема OK или ERROR понимаем отправлено СМС или нет
      t_last_command = millis();  
      #ifndef NOSERIAL        
        Serial.print("Sending SMS ");
        Serial.print("flag_modem_resp = "); Serial.println(String(flag_modem_resp));   
      #endif
      // Находим последний перенос строки, перед статусом
    }
    //********** проверка приема SMS ***********
    else if (_response.indexOf(F("+CMTI:")) > -1) {       // Пришло сообщение о приеме SMS
      #ifndef NOSERIAL       
        Serial.println("Incoming SMS");
      #endif  
      int index = _response.lastIndexOf(',');   // Находим последнюю запятую, перед индексом
      String result = _response.substring(index + 1, _response.length()); // Получаем индекс
      result.trim();                            // Убираем пробельные символы в начале/конце
      #ifndef NOSERIAL        
        Serial.print("new mess "); Serial.println(result);
      #endif
      //Добавляем текущую СМС в очередь на обработку
      add_in_queue_SMS(result.toInt());
    }
    else if (_response.indexOf(F("+CMGR:")) > -1) {    // Пришел текст SMS сообщения 
        _response += '\r' + SIM800.readStringUntil('\n'); //.readString();  читаем до конца строки (без OK) 
        {
           //Serial.print(" ======= _response  "); Serial.print(_response); Serial.println(" =======");          
           String  temp_in = SIM800.readString(); // если модем прислал текст сообщения, дочитываем до OK и закрываем команду
           //Serial.print(" ======= temp_in  "); Serial.print(temp_in);  Serial.println(" =======");          
        }
        comand_OK = true;    
       // parseSMS(_response);        // Распарсить SMS на элементы
    }
    else if (_response.indexOf(F("+CPBS:")) > -1){ // выяснить количество занятых номеров на СИМ и общее возможное количество
      uint8_t index1 = _response.indexOf(',') + 1;   // Находим запятую, перед количеством имеющихся номеров
      uint8_t index2 = _response.lastIndexOf(',');   // Находим последнюю запятую, перед общим количеством номеров
     String used_num = _response.substring(index1, index2);
        alloc_num[0] = used_num.toInt();
        used_num = _response.substring(index2+1);
        alloc_num[1] = used_num.toInt();
      #ifndef NOSERIAL          
        Serial.print("exist_numer - "); 
        Serial.print( alloc_num[0]);  
        Serial.print(" : total_numer - "); 
        Serial.println( alloc_num[1]); 
      #endif        
    }
    else if (_response.indexOf(F("+CPBF:")) > -1) { // +CPBF: 4,"078123456",129,"078123456Manip"
      String phonen_index; 
        firstIndex = _response.indexOf(',');
        phonen_index = _response.substring(7, firstIndex); 
        textnumber =  _response.substring(firstIndex+2, firstIndex+2+DIGIT_IN_PHONENAMBER);
        textnumbercomment =_response.substring(_response.lastIndexOf(',')+2, _response.lastIndexOf('\"'));
           #ifndef NOSERIAL     
             Serial.print("File String +CPBF: index= "); Serial.print(phonen_index); 
             Serial.print(" ; number= "); Serial.print(textnumber);                          
             Serial.print(" ; comment= "); Serial.print(textnumbercomment); 
             Serial.print(" flag_modem_resp = "); Serial.println(flag_modem_resp);              
           #endif 
      if (flag_modem_resp == 2)  // одиночный поиск номера из СМС 
       {
        SMS_phoneBookIndex = phonen_index.toInt();
           #ifndef NOSERIAL   
             Serial.print("+CPBF: INT index= "); Serial.println(SMS_phoneBookIndex);              
           #endif            
       // t_last_command = millis();  
       }
    }    
    else if (_response.indexOf(F("+SAPBR:")) > -1){
         if (_response.indexOf(F("+SAPBR: 1,1")) > -1) 
            {
               GPRS_ready = true;
        #ifndef NOSERIAL   
          Serial.println("+SAPBR OBMEN OK GPRS_ready *********************************** !!!!");
        #endif               
              }
         else if (_response.indexOf(F("+SAPBR: 1,2")) > -1 || _response.indexOf(F("+SAPBR: 1,3")) > -1)
           {
              GPRS_ready = false;
        #ifndef NOSERIAL   
          Serial.println("+SAPBR OBMEN NOT GPRS_ready xxxxxxxxxxxxxxxxxxxx !!!!");
        #endif             
           }
       }  
    else if (_response.indexOf(F("+HTTPACTION:")) > -1){
        if (_response.indexOf(F("+HTTPACTION: 0,200")) > -1) {
           GET_GPRS_OK = true;
        // #ifndef NOSERIAL   
        //   Serial.println("OBMEN OK GET_GPRS_OK *********************************** !!!!");
        // #endif             
           }
         else {GET_GPRS_OK = false;
        // #ifndef NOSERIAL   
        //   Serial.println("OBMEN NOT GET_GPRS_OK xxxxxxxxxxxxxxxxxxxx !!!!");
        // #endif           
          }
       } 
    else if (_response.indexOf(F("+HTTPREAD:")) > -1) { 
          _response = SIM800.readStringUntil('\n');
         #ifndef NOSERIAL   
           Serial.print("   HTTPREAD "); Serial.println(_response);
         #endif          
        //  Ans_parse(_response); //Разбор ответа от Сервера
     }                             

    if (_response.indexOf(F("OK")) > -1) { // если происходит соединение с MQTT сервером отследить CONNECT OK
      comand_OK = true;
      String SMSResp_Mess;

     }
     if (_response.indexOf(F("ERROR")) > -1) {
      if (SMS_currentIndex != 0){
        #ifndef NOSERIAL        
         Serial.println ("Message was not sent. Error");
        #endif
        //flag_modem_resp=0;  
      }
      else if (flag_modem_resp==2) // завершен одиночный поиск номера из СМС - приступить к выполнению команды
        flag_modem_resp=0;   
      else if (flag_modem_resp==8) {// Неудачное подключение  TCP / сервер не отвечае не правильный IP - закрыть соединение
        if ( TCP_ready)  add_in_queue_comand(30,"+CIPCLOSE",-1); // Закрыть текущее соединение               
         MQTT_connect = false;  TCP_ready=false;
        flag_modem_resp=0;
      }    
      if (_response.indexOf(F("SIM not inserted")) > -1) {
         SIM_fatal_error=true;
        #ifndef NOSERIAL        
         Serial.println ("SIM FATAL ERROR");
        #endif      
      }  
     }
    //*************************************************
    // Обработка MQTT
    if ( TCP_ready){
     
      if (_response[0] == 0x30)  {  MQTT_connect = true; //print_MQTTrespons_to_serial(_response); // пришел ответ на публикацию в подписанном топике
         String s1; 
            s1 += _response.substring(4 , 4 + _response[3]);                 
        char _topik_path[s1.length()+1];
        for (int k=0; k < s1.length()+1; ++k) _topik_path[k]=s1[k];
         s1 = _response.substring(4 + _response[3]);
        byte* _payload;
        byte sb1 = (byte)s1[0];
        _payload = &sb1;
          mqttCallback(_topik_path, _payload, 1);
        }
      else if (_response[0] == 0x20 || _response[0] == 0x90) { MQTT_connect = true; } // print_MQTTrespons_to_serial(_response);}// пришло сообщение о подключении или удачной подписке на топик
      //else if (_response[0] == 0x40) {print_MQTTrespons_to_serial(_response);} //пришло сообщение о удачной публикации топика
      else if (_response.indexOf(F("CLOSED")) > -1) { MQTT_connect = false;  TCP_ready=false;}
      else if (_response[0] == 0xD0) { MQTT_connect = true; } //print_MQTTrespons_to_serial(_response);} // подтверждения ping от MQTT сервера
     }  
  }

  #ifndef NOSERIAL   
    if (Serial.available())   {                   // Ожидаем команды по Serial...
       SIM800.write(Serial.read());                // ...и отправляем полученную команду модему
    }
  #endif

// опросить кнопку и переключить LED
  static bool btnLast;
      bool btnPressed = debounceRead(36, 20);

      if (btnPressed != btnLast) {
        if ((btnLevel & 0x80) != 0) { // Switch
          switchRelay(14, btnPressed);
        } else { // Button
          if (btnPressed)
            toggleRelay(14);
        }
        // Serial.print(F("Button "));
        // Serial.println(btnPressed ? F("pressed") : F("released"));
        btnLast = btnPressed;
      }

   static unsigned long t_Led; 
   static int count_led; // счетчик миганий
   static int16_t frequency_led; // время смены состояния
   static int16_t next_led;
   if (count_led == 0) { //исходное состояние
     frequency_led=1000;
   }

   if (millis()-t_Led > next_led) {

      if ( modemOK ) {
       if (count_led == -1) {
          if (MQTT_connect ) {count_led=3; frequency_led=400;} //моргает 4 раз потом пауза 
          else if (GPRS_ready) {count_led=2; frequency_led=400;} //моргает 3 раз потом пауза           
        } 
       else if (count_led < -1) {
        count_led=1; frequency_led=1000; }
       }      
      else {
         if ( count_led == -1) {count_led=4; frequency_led=400; //моргает 5 раз потом пауза
           if (SIM_fatal_error) {count_led=7; frequency_led=400;} //моргает 8 раз потом пауза      
         }  
      }

        digitalWrite(12, !digitalRead(12));
//        toggleRelay(12);
        t_Led=millis();  
        if (!digitalRead(12)) {--count_led; next_led=frequency_led;} 
        else next_led=400;
     // Serial.print("count_led = ");Serial.print(count_led); Serial.print(" frequency_led = ");Serial.println(frequency_led);
   } 
}

void print_MQTTrespons_to_serial(const String& _resp){
    #ifndef NOSERIAL
     for (int h=0; h<_resp.length();++h){
         Serial.print(_resp[h], HEX); Serial.print(" ");
     }
      Serial.println();
    #endif  
}

// процедура выясняет количество имеющихся номеров в книге и общее возможное количество и сохранет их в массив alloc_num[]
void exist_numer(){
  add_in_queue_comand(30,"+CPBS?",0);
  return;
}

// Если звонок от БЕЛОГО номера - ответить, включить реле и сбросить вызов
void regular_call(){  
  add_in_queue_comand(30,"A", -1) ; // отвечаем на вызов 
  add_in_queue_comand(30, "H", -1);  // Завершаем вызов
  toggleRelay(0); // переключаем RELAY
}

void retGetZapros(){
      add_in_queue_comand(30,"+HTTPTERM",-1); // Закрыть текущий запрос
     #ifndef NOSERIAL          
           Serial.print("GPRS_ready = false; "); 
     #endif 
}

void retTCPconnect(){
      add_in_queue_comand(30,"+CIPCLOSE",-1); // Закрыть текущий запрос
      TCP_ready = false; MQTT_connect=false; 
     #ifndef NOSERIAL          
           Serial.print("TCP_ready = false; "); 
     #endif 
}

void switchRelay(int8_t id, bool on) {
  bool relay;
  if (id != 14 && id != 12) {// переключить LED из массива
   relay = digitalRead(relayPin[id]);

  if (relay != on) {
     digitalWrite(relayPin[id], HIGH == on);

    if (MQTT_connect) {
      String topic;
      if (_mqttClient != strEmpty) {
        topic += _mqttClient;
      }
      topic += FPSTR(mqttRelayTopic);
      //**********  добавляем топик подверждения
      topic += FPSTR(mqttRelayConfirmTopic);
      topic += charSlash;
      topic += String(id + 1);
      mqttPublish(topic, String(on));
    }
  }
  }
  else {// переключить LED 12, 14
     relay = digitalRead(id);
      if (relay != on) digitalWrite(id, HIGH == on);
  }

}

inline void toggleRelay(int8_t id) {
  if (id !=12 && id != 14)
   switchRelay(id, !digitalRead(relayPin[id]));
  else
   switchRelay(id, !digitalRead(id));
}

bool debounceRead(int8_t id, uint32_t debounceTime) {
  if (! debounceTime)
    return (digitalRead(id) == (btnLevel & 0x01));

  if (digitalRead(id) == (btnLevel & 0x01)) { // Button pressed
    uint32_t startTime = millis();
    while (millis() - startTime < debounceTime) {
      if (digitalRead(id) != (btnLevel & 0x01))
        return false;
      delay(1);
    }
    return true;
  }
  return false;
}